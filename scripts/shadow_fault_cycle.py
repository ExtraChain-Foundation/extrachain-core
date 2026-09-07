#!/usr/bin/env python3
"""Run one bounded Shadow fault cycle inside a private Linux network namespace.

Use the validation Python environment; the script calls unshare -Urn. Each cycle uses
a new work directory. The normal soak harness audits all seven recovered nodes.
"""
import argparse
from contextlib import closing
import json
import os
from pathlib import Path
import re
import signal
import sqlite3
import subprocess
import sys
import time

import msgpack

from shadow_receipts import SUBMISSION, audit, decode_store_payload


class Cycle:
    def __init__(self, args):
        self.args = args
        self.work = args.work.resolve()
        self.barrier = self.work / 'barrier'
        self.binary = args.build.resolve() / 'extrachain-node-run'
        self.events = []
        self.children = []
        self.stopped = set()
        self.filters = set()
        self.deadline = time.monotonic() + 300
        self.environment = dict(os.environ, EXTRACHAIN_TEST_BUILD=str(args.build.resolve()),
                                EXC_SHADOW_WORK=str(self.work), EXC_SHADOW_EXTERNAL_CONTROL='1',
                                EXC_SHADOW_SENDERS='6', EXC_SHADOW_PER_SENDER='48',
                                EXC_SHADOW_NODE_COUNT='7', EXC_SHADOW_ALLOWED_DEAD='0',
                                EXC_SHADOW_RUN_SECONDS='300', EXC_SHADOW_DEADLINE_S='300',
                                EXC_SHADOW_MIN_RUNTIME_S='0', EXC_SHADOW_DFS_BYTES='4194304',
                                EXC_SHADOW_TOPOLOGY=args.topology,
                                EXC_SHADOW_RECEIPTS_PYTHON=sys.executable)

    def event(self, name, **values):
        entry = dict(event=name, monotonic_ms=time.monotonic_ns() // 1_000_000, **values)
        self.events.append(entry)
        (self.work / 'fault-events.json').write_text(json.dumps(self.events, indent=2) + '\n')
        print(json.dumps(entry), flush=True)

    def home(self, index):
        return self.work / 'bootstrap' / ('server' if index == 0 else f'client{index}')

    def pid(self, index):
        path = self.barrier / f'pid-{index}'
        if not path.exists():
            path = self.barrier / f'initial-pid-{index}'
        return int(path.read_text())

    def send(self, index, sig):
        pid = self.pid(index)
        command = Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
        if command[:2] != [os.fsencode(self.binary), b'committee']:
            raise RuntimeError(f'PID {pid} is not the expected committee process')
        os.kill(pid, sig)
        self.event('signal', node=index, pid=pid, signal=sig.name)

    def wait_for(self, predicate, description, timeout=60):
        deadline = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < deadline:
            if self.harness.poll() is not None:
                raise RuntimeError(f'Soak exited while waiting for {description}')
            if predicate():
                return
            time.sleep(0.1)
        raise RuntimeError(f'Timeout waiting for {description}')

    def submitted(self):
        hashes = set()
        for index in range(7):
            path = self.work / f'node-{index}.log'
            if path.exists():
                hashes.update(match[0] for match in SUBMISSION.findall(path.read_text(errors='replace')))
        return len(hashes)

    def leader(self):
        path = self.home(0) / 'data/consensus'
        validators = msgpack.unpackb((path / 'validator-set.msgpack').read_bytes(), raw=False)[3]
        active = sorted((record for record in validators if record[10] == 0), key=lambda record: record[3])
        with closing(sqlite3.connect(f'{(path / "safety.sqlite").as_uri()}?mode=ro', uri=True)) as database:
            payload = database.execute("SELECT payload FROM safety_state WHERE slot='active'").fetchone()[0]
        state = decode_store_payload(payload)
        height, round_number = state[7][3] + 1, state[10]
        identifier = active[(height + round_number) % len(active)][7]
        for index in range(7):
            if f'local node identifier={identifier} ' in (self.work / f'node-{index}.log').read_text():
                self.event('leader', node=index, height=height, round=round_number)
                return index
        raise RuntimeError('Current leader is absent from the committee')

    def restart(self, index):
        remaining = max(10, int(self.deadline - time.monotonic()))
        command = [str(self.binary), 'committee', 'data', 'seed' if index == 0 else 'joiner',
                   str(index), str(self.args.port + 20 + index), str(self.args.port + 20),
                   '7', '0', str(remaining), str(self.barrier), '1', '1']
        environment = dict(self.environment, EXC_DEBUG_LOG='1', EXC_BIND_IP=f'127.0.0.{index + 1}',
                           EXC_DFS_BYTES='0')
        environment.pop('EXC_FUND_NODES', None)
        with (self.work / f'node-{index}.log').open('ab') as log:
            process = subprocess.Popen(command, cwd=self.home(index), env=environment, stdout=log,
                                       stderr=subprocess.STDOUT)
        self.children.append(process)
        (self.barrier / f'pid-{index}').write_text(f'{process.pid}\n')
        self.event('restart', node=index, pid=process.pid)

    @staticmethod
    def tc(*arguments):
        result = subprocess.run(['tc', *arguments], text=True, capture_output=True)
        if result.returncode:
            raise RuntimeError(f'tc failed: {result.stderr.strip()}')
        return result.stdout

    def partition_filters(self):
        # An outbound loopback socket uses 127.0.0.1 for every process. Resolve
        # its owner by inode so the partition follows nodes, not source IPs.
        owners = {}
        for index in range(7):
            directory = Path(f'/proc/{self.pid(index)}/fd')
            for entry in directory.iterdir():
                try:
                    match = re.fullmatch(r'socket:\[(\d+)\]', os.readlink(entry))
                except FileNotFoundError:
                    continue
                if match:
                    owners[match[1]] = index
        for line in Path('/proc/net/tcp').read_text().splitlines()[1:]:
            fields = line.split()
            source = int(fields[1].split(':')[1], 16)
            destination = int(fields[2].split(':')[1], 16)
            peer = destination - self.args.port - 20
            owner = owners.get(fields[9])
            if owner is None or not 0 <= peer < 7 or (owner < 3) == (peer < 3):
                continue
            for ports in ((source, destination), (destination, source)):
                if ports in self.filters:
                    continue
                self.tc('filter', 'add', 'dev', 'lo', 'egress', 'protocol', 'ip', 'pref',
                        str(len(self.filters) + 1), 'flower', 'ip_proto', 'tcp',
                        'src_port', str(ports[0]), 'dst_port', str(ports[1]), 'action', 'drop')
                self.filters.add(ports)

    def partition(self, seconds):
        self.tc('qdisc', 'add', 'dev', 'lo', 'clsact')
        self.partition_filters()
        self.event('partition-start', groups=[[0, 1, 2], [3, 4, 5, 6]], filters=len(self.filters))
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            self.partition_filters()
            time.sleep(0.1)
        stats = self.tc('-s', 'filter', 'show', 'dev', 'lo', 'egress')
        (self.work / 'partition-statistics.txt').write_text(stats)
        dropped = sum(map(int, re.findall(r'dropped (\d+)', stats)))
        self.tc('qdisc', 'del', 'dev', 'lo', 'clsact')
        self.event('partition-end', dropped=dropped, filters=len(self.filters))
        if dropped == 0:
            raise RuntimeError('Partition did not drop any packets')

    def faults(self):
        duration = 6 + self.args.iteration % 3
        if self.args.kind == 'freeze':
            index = self.args.iteration % 7
            self.send(index, signal.SIGSTOP)
            self.stopped.add(index)
            time.sleep(duration)
            self.send(index, signal.SIGCONT)
            self.stopped.remove(index)
        elif self.args.kind in ('kill-leader', 'restart'):
            # Preserve the workload: kill only after all submissions were
            # accepted. Recovery must still finalize them on every node.
            self.wait_for(lambda: self.submitted() == 288, 'all workload submissions')
            pending = audit(self.work, 288, {}, {}, 7)
            self.event('workload-before-stop', submitted=pending['submitted'],
                       finalized=[node['finalized'] for node in pending['nodes']])
            if pending['complete']:
                raise RuntimeError('Workload completed before the process-stop fault')
            index = self.leader() if self.args.kind == 'kill-leader' else self.args.iteration % 7
            self.send(index, signal.SIGKILL if self.args.kind == 'kill-leader' else signal.SIGTERM)
            time.sleep(duration)
            pid = self.pid(index)
            self.wait_for(lambda: not Path(f'/proc/{pid}').exists()
                          or Path(f'/proc/{pid}/stat').read_text().split()[2] == 'Z', 'node shutdown', 15)
            self.restart(index)
        elif self.args.kind == 'partition':
            self.partition(duration)
        else:
            self.tc('qdisc', 'add', 'dev', 'lo', 'root', 'netem', 'delay', '150ms', '50ms',
                    'loss', '2%')
            self.event('netem-start', delay_ms=150, jitter_ms=50, loss_percent=2)
            self.partition(duration)
            index = self.args.iteration % 7
            self.send(index, signal.SIGSTOP)
            self.stopped.add(index)
            time.sleep(3)
            self.send(index, signal.SIGCONT)
            self.stopped.remove(index)
            (self.work / 'netem-statistics.txt').write_text(self.tc('-s', 'qdisc', 'show', 'dev', 'lo'))
            self.tc('qdisc', 'del', 'dev', 'lo', 'root')
            self.event('netem-end')

    def run(self):
        if os.readlink('/proc/self/ns/net') == self.args.parent_netns:
            raise RuntimeError('Use unshare -Urn; network faults require a private namespace')
        self.work.mkdir(parents=True, exist_ok=False)
        subprocess.run(['ip', 'link', 'set', 'lo', 'up'], check=True)
        script = Path(__file__).with_name('shadow_soak.sh')
        with (self.work / 'soak.log').open('w') as log:
            self.harness = subprocess.Popen(['bash', str(script), str(self.args.seed.resolve()),
                                             str(self.args.port)], env=self.environment, stdout=log,
                                            stderr=subprocess.STDOUT)
        try:
            self.wait_for(lambda: (self.barrier / 'committee-ready').exists(), 'committee barrier', 300)
            self.deadline = time.monotonic() + 300
            (self.barrier / 'controller-ready').touch()
            self.wait_for(lambda: (self.barrier / 'go').exists(), 'workload start')
            self.event('cycle-start', kind=self.args.kind, iteration=self.args.iteration)
            self.wait_for(lambda: self.submitted() > 0, 'first workload submission')
            before_fault = audit(self.work, 288, {}, {}, 7)
            if before_fault['complete']:
                raise RuntimeError('Workload completed before fault injection')
            self.event('workload-at-fault', submitted=before_fault['submitted'],
                       finalized=[node['finalized'] for node in before_fault['nodes']])
            self.faults()
            (self.barrier / 'faults-done').touch()
            self.event('faults-done')
            # The cycle budget covers recovery. Offline audits can run after it.
            status = self.harness.wait(timeout=max(1, self.deadline - time.monotonic()) + 90)
            self.event('cycle-end', status=status)
            return status
        finally:
            for index in self.stopped:
                self.send(index, signal.SIGCONT)
            for scope in ('clsact', 'root'):
                subprocess.run(['tc', 'qdisc', 'del', 'dev', 'lo', scope], capture_output=True)
            if self.harness.poll() is None:
                self.harness.terminate()
                self.harness.wait(timeout=30)
            for child in self.children:
                if child.poll() is None:
                    child.terminate()
                child.wait(timeout=30)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=('freeze', 'kill-leader', 'restart', 'partition', 'mixed'), required=True)
    parser.add_argument('--iteration', type=int, choices=range(8), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--seed', type=Path, required=True)
    parser.add_argument('--port', type=int, default=20440)
    parser.add_argument('--topology', choices=('mesh', 'ring', 'chain', 'degree3'), default='mesh')
    parser.add_argument('--parent-netns', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65495:
        parser.error('--port must be between 1024 and 65495')
    if args.parent_netns is None:
        return subprocess.call(['unshare', '-Urn', sys.executable, str(Path(__file__).resolve()),
                                *sys.argv[1:], '--parent-netns', os.readlink('/proc/self/ns/net')])
    return Cycle(args).run()


if __name__ == '__main__':
    raise SystemExit(main())
