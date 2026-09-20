#!/usr/bin/env python3
"""Run repeated DAG and DFS workloads with faults, restart and a returning observer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

import msgpack

from shadow_fault_cycle import Cycle
from shadow_live_join import DOCUMENTS
from shadow_receipts import audit
from shadow_wave_audit import audit_waves
from shadow_verify import capture_checkpoint, node_dirs


class Endurance(Cycle):
    def __init__(self, args):
        super().__init__(args)
        self.wave = 0
        self.waves = args.duration // args.interval
        self.observer = None
        self.observer_online = False
        self.offline = set()
        self.submissions, self.cursors, self.file_cache = {}, {}, {}
        self.next_sample = 0
        self.last_minted = 0
        self.deadline = time.monotonic() + args.duration + args.recovery + 600
        self.environment.update(
            EXC_SHADOW_WAVES=str(self.waves), EXC_SHADOW_SENDERS=str(args.senders),
            EXC_SHADOW_PER_SENDER=str(args.per_sender), EXC_SHADOW_DFS_BYTES=str(args.file_bytes),
            EXTRACHAIN_TEST_DFS_BYTES=str(args.file_bytes),
            EXC_SHADOW_VECTOR_ROWS='48', EXC_SHADOW_VECTOR_CROSS='8', EXC_SHADOW_HISTORY_ROWS='130',
            EXC_SHADOW_REMOVE_AFTER_S='30', EXC_SHADOW_DFS_MODE='full', EXC_SHADOW_MINING_TEST='1',
            EXC_SHADOW_MINING_LONG_TEST='1',
            EXC_SHADOW_CHECKPOINT=str(self.barrier / 'shutdown-checkpoint.json'),
            EXC_SHADOW_RUN_SECONDS=str(args.duration + args.recovery + 600),
            EXC_SHADOW_DEADLINE_S=str(args.duration + args.recovery), EXC_SHADOW_HOLD_S='0')
        for key in ('EXC_SHADOW_RESUME_WAVE', 'EXC_SHADOW_OLD_INDEXES', 'EXC_FUND_NODES'):
            self.environment.pop(key, None)

    def sample(self):
        now = time.monotonic()
        if now < self.next_sample:
            return
        self.next_sample = now + 5
        free = shutil.disk_usage(self.work).free
        if free < 10 * 1024 ** 3:
            raise RuntimeError('Free disk space fell below 10 GiB')
        nodes = []
        for index in range(8 if self.observer_online else 7):
            if index in self.offline:
                continue
            if not (self.barrier / f'pid-{index}').exists() and not (
                    self.barrier / f'initial-pid-{index}').exists():
                continue
            pid = self.pid(index)
            directory = Path(f'/proc/{pid}')
            try:
                if directory.joinpath('cmdline').read_bytes().split(b'\0')[:2] != [
                        os.fsencode(self.binary), b'committee']:
                    raise RuntimeError(f'Node {index} exited or its PID changed')
                fields = dict(line.split(':', 1) for line in directory.joinpath('status').read_text().splitlines())
                rss = int(fields.get('VmRSS', '0 kB').split()[0]) * 1024
                threads = int(fields['Threads'])
                fds = sum(1 for _ in directory.joinpath('fd').iterdir())
                process_stat = directory.joinpath('stat').read_text().rsplit(')', 1)[1].split()
                log_bytes = (self.work / f'node-{index}.log').stat().st_size
            except FileNotFoundError as error:
                raise RuntimeError(f'Node {index} disappeared during resource sampling') from error
            if rss > 4 * 1024 ** 3 or fds > 4096 or log_bytes > 16 * 1024 ** 3:
                self.event('resource-limit', node=index, pid=pid, rss_bytes=rss,
                           fds=fds, log_bytes=log_bytes)
                raise RuntimeError(f'Node {index} exceeded its RSS, file descriptor or log limit: '
                                   f'rss_bytes={rss}, fds={fds}, log_bytes={log_bytes}')
            nodes.append(dict(node=index, pid=pid, rss_bytes=rss, threads=threads, fds=fds,
                              log_bytes=log_bytes, cpu_ticks=int(process_stat[11]) + int(process_stat[12]),
                              start_ticks=int(process_stat[19])))
        with (self.work / 'resources.jsonl').open('a') as stream:
            stream.write(json.dumps(dict(monotonic_s=now, disk_free_bytes=free, nodes=nodes)) + '\n')

    def wait_for(self, predicate, description, timeout=60):
        deadline = min(self.deadline, time.monotonic() + timeout)
        while time.monotonic() < deadline:
            if self.harness.poll() is not None:
                raise RuntimeError(f'Soak exited while waiting for {description}')
            self.sample()
            if predicate():
                return
            time.sleep(0.5)
        raise TimeoutError(description)

    def hold_until(self, deadline):
        if time.monotonic() < deadline:
            self.wait_for(lambda: time.monotonic() >= deadline, 'scheduled wave',
                          max(1, deadline - time.monotonic() + 2))

    def start(self, command, log, cwd, environment):
        with log.open('ab') as stream:
            child = subprocess.Popen(command, cwd=cwd, env=environment, stdout=stream,
                                     stderr=subprocess.STDOUT)
        self.children.append(child)
        return child

    def start_member(self, index, resume=True):
        remaining = max(10, int(self.deadline - time.monotonic()) + 120)
        count = self.args.per_sender if index < self.args.senders else 0
        environment = dict(self.environment, EXC_BIND_IP=f'127.0.0.{index + 1}', EXC_DEBUG_LOG='1',
                           EXC_DFS_BYTES=str(self.args.file_bytes), EXC_DFS_HISTORY_ROWS='0',
                           EXC_DFS_VECTOR_ROWS='0', EXC_DFS_VECTOR_CROSS='0', EXC_DFS_REMOVE_AFTER_S='0',
                           EXC_DFS_MODE='full')
        environment.pop('EXC_FUND_NODES', None)
        if index == 7:
            environment.update(EXC_SHADOW_WAVES='1', EXC_DFS_BYTES='0')
            environment.pop('EXC_SHADOW_RESUME_WAVE', None)
        elif resume:
            environment['EXC_SHADOW_RESUME_WAVE'] = str(self.wave)
        child = self.start([str(self.binary), 'committee', 'data', 'seed' if index == 0 else 'joiner',
                            str(index), str(self.args.port + 20 + index), str(self.args.port + 20),
                            '8' if index == 7 else '7', str(count), str(remaining), str(self.barrier), '1', '1'],
                           self.work / f'node-{index}.log', self.home(index), environment)
        (self.barrier / f'pid-{index}').write_text(str(child.pid))
        self.offline.discard(index)
        self.event('restart' if resume else 'observer-start', node=index, pid=child.pid, wave=self.wave)
        return child

    def stop_member(self, index, hard=False):
        pid = self.pid(index)
        self.send(index, signal.SIGKILL if hard else signal.SIGTERM)
        self.offline.add(index)

        def stopped():
            path = Path(f'/proc/{pid}/stat')
            try:
                return path.read_text().rsplit(')', 1)[1].split()[0] == 'Z'
            except FileNotFoundError:
                return True

        self.wait_for(stopped, f'node {index} shutdown', 30)
        for child in self.children:
            if child.pid == pid:
                child.wait(timeout=1)

    def fresh_observer(self):
        home = self.home(7) / 'data'
        home.mkdir(parents=True, exist_ok=False)
        (home / 'consensus').mkdir()
        for document in DOCUMENTS:
            shutil.copy2(self.home(0) / 'data/consensus' / f'{document}.msgpack',
                         home / 'consensus' / f'{document}.msgpack')
        tip = int(json.loads((self.home(0) / 'data/dag/range').read_text())['last'])
        self.event('fresh-join', copied_dag=False, copied_dfs=False, copied_documents=list(DOCUMENTS),
                   committee_tip=tip, target=tip + 1)
        environment = dict(self.environment, EXC_BIND_IP='127.0.0.8', EXC_DFS_BYTES='0')
        environment.pop('EXC_FUND_NODES', None)
        self.join = self.start([str(self.binary), 'join', 'data', '127.0.0.7', str(tip + 1),
                                str(self.args.port + 27), str(self.args.port + 26)],
                               self.work / 'fresh-join.log', self.home(7), environment)

    def finish_join(self):
        self.wait_for(lambda: self.join.poll() is not None, 'fresh observer catch-up', self.args.recovery)
        if self.join.returncode != 0:
            raise RuntimeError(f'Fresh observer failed with {self.join.returncode}')
        self.observer = self.start_member(7, resume=False)
        self.observer_online = True

    def converge(self, shutdown=False):
        nodes = 8 if self.observer_online else 7
        expected = (self.wave + 1) * self.args.senders * self.args.per_sender
        mining_progress = []

        def complete():
            if self.observer_online and self.observer.poll() is not None:
                raise RuntimeError('Observer stopped before convergence')
            receipts = audit(self.work, expected, self.submissions, self.cursors, nodes)
            files = audit_waves(self.work, self.wave + 1, nodes, self.args.file_bytes, self.file_cache)
            (self.work / 'live-wave-receipts.json').write_text(json.dumps(receipts, indent=2))
            (self.work / 'live-wave-files.json').write_text(json.dumps(files, indent=2))
            if not receipts['complete'] or not files['complete']:
                return False
            mining_progress.clear()
            for index in range(nodes):
                try:
                    with (self.home(index) / 'data/consensus/mining-state.msgpack').open('rb') as stream:
                        data = stream.read(16 * 1024 * 1024 + 1)
                except FileNotFoundError:
                    return False
                if len(data) > 16 * 1024 * 1024:
                    raise RuntimeError('Mining snapshot exceeds its byte limit')
                snapshot = msgpack.unpackb(data, raw=False, strict_map_key=False,
                                           max_array_len=8192, max_map_len=8192,
                                           max_str_len=1048576, max_bin_len=1048576)
                if not isinstance(snapshot, list) or len(snapshot) != 2:
                    raise RuntimeError('Invalid mining snapshot')
                state = snapshot[1]
                if (not isinstance(state, list) or len(state) != 7
                        or not all(type(value) is int for value in state[1:4])
                        or not 0 <= state[3] <= state[2] <= 10000 * 100000000):
                    raise RuntimeError('Invalid mining counters')
                mining_progress.append(dict(node=index, section=state[1], reserved_units=state[2],
                                            minted_units=state[3]))
            if min(item['minted_units'] for item in mining_progress) <= self.last_minted:
                return False
            if shutdown:
                checkpoint = capture_checkpoint(node_dirs(self.work), mining_progress)
                if checkpoint is None:
                    return False
                with (self.barrier / 'shutdown-checkpoint.json').open('x') as stream:
                    json.dump(checkpoint, stream, indent=2)
                self.event('shutdown-checkpoint', **checkpoint)
            return True

        self.wait_for(complete, f'wave {self.wave} convergence on {nodes} nodes', self.args.recovery)
        self.last_minted = max(item['minted_units'] for item in mining_progress)
        self.event('wave-converged', wave=self.wave, nodes=nodes, receipts=expected,
                   file_copies=(self.wave + 1) * 7 * nodes, mining=mining_progress)

    def fault(self):
        pending = audit(self.work, (self.wave + 1) * self.args.senders * self.args.per_sender,
                        self.submissions, self.cursors, 7)
        self.event('workload-at-fault', wave=self.wave, submitted=pending['submitted'],
                   finalized=[node['finalized'] for node in pending['nodes']])
        kind = self.wave % 4
        if kind == 0:
            index = self.wave % 7
            self.send(index, signal.SIGSTOP)
            self.stopped.add(index)
            self.hold_until(time.monotonic() + 6)
            self.send(index, signal.SIGCONT)
            self.stopped.remove(index)
        elif kind == 1:
            index = self.leader()
            self.stop_member(index, hard=True)
            self.hold_until(time.monotonic() + 6)
            self.start_member(index)
        elif kind == 2:
            if self.observer_online:
                self.send(7, signal.SIGSTOP)
                self.stopped.add(7)
            self.filters.clear()
            self.partition(8)
            if self.observer_online:
                self.send(7, signal.SIGCONT)
                self.stopped.remove(7)
        else:
            self.tc('qdisc', 'add', 'dev', 'lo', 'root', 'netem', 'delay', '150ms', '50ms', 'loss', '2%')
            self.event('netem-start', delay_ms=150, jitter_ms=50, loss_percent=2)
            self.hold_until(time.monotonic() + 8)
            with (self.work / 'netem-statistics.txt').open('a') as stream:
                stream.write(self.tc('-s', 'qdisc', 'show', 'dev', 'lo'))
            self.tc('qdisc', 'del', 'dev', 'lo', 'root')
            self.event('netem-end')

    def run(self):
        if os.readlink('/proc/self/ns/net') == self.args.parent_netns:
            raise RuntimeError('Network faults require a private namespace')
        self.work.mkdir(parents=True, exist_ok=False)
        subprocess.run(['ip', 'link', 'set', 'lo', 'up'], check=True)
        script = Path(__file__).with_name('shadow_soak.sh')
        with (self.work / 'soak.log').open('w') as stream:
            self.harness = subprocess.Popen(['bash', str(script), str(self.args.seed.resolve()),
                                             str(self.args.port)], env=self.environment, stdout=stream,
                                            stderr=subprocess.STDOUT)
        try:
            with self.binary.open('rb') as stream:
                binary_hash = hashlib.file_digest(stream, 'sha256').hexdigest()
            self.event('configuration', duration_s=self.args.duration, interval_s=self.args.interval,
                       waves=self.waves, binary_sha256=binary_hash, senders=self.args.senders,
                       per_sender=self.args.per_sender, file_bytes=self.args.file_bytes,
                       bootstrap_file_bytes=self.args.file_bytes, audit_timeout_s=self.args.audit_timeout)
            self.wait_for(lambda: (self.barrier / 'committee-ready').exists(), 'committee barrier', 600)
            (self.barrier / 'controller-ready').touch()
            self.wait_for(lambda: (self.barrier / 'go').exists(), 'workload start')
            started = time.monotonic()
            self.deadline = started + self.args.duration + self.args.recovery
            self.event('endurance-start')
            offline_wave = max(2, self.waves // 3)
            online_wave = min(self.waves - 1, offline_wave + max(1, min(3, self.waves // 4)))
            for self.wave in range(self.waves):
                self.hold_until(started + self.wave * self.args.interval)
                if self.wave == 1:
                    self.fresh_observer()
                if self.wave == offline_wave:
                    self.stop_member(7)
                    self.observer_online = False
                    self.event('observer-offline', return_wave=online_wave)
                if self.wave > 0:
                    temporary = self.barrier / 'wave.new'
                    temporary.write_text(str(self.wave))
                    temporary.replace(self.barrier / 'wave')
                self.wait_for(lambda: all((self.barrier / f'loaded-wave-{self.wave}-{index}').exists()
                                          for index in range(7)), 'wave submissions', self.args.recovery)
                self.event('wave-loaded', wave=self.wave)
                self.fault()
                if self.wave == 1:
                    self.finish_join()
                if self.wave == online_wave:
                    self.observer = self.start_member(7)
                    self.observer_online = True
                    self.event('observer-returned', missed_waves=online_wave - offline_wave + 1)
                self.converge()
            self.hold_until(started + self.args.duration)
            self.converge(shutdown=True)
            elapsed = time.monotonic() - started
            # The harness stops every barrier PID together after its live audits.
            # Stopping the observer first lets the committee advance during those audits.
            (self.barrier / 'faults-done').touch()
            self.event('offline-audit-start', committee_timeout_s=self.args.recovery + self.args.audit_timeout,
                       observer_timeout_s=self.args.audit_timeout)
            status = self.harness.wait(timeout=self.args.recovery + self.args.audit_timeout)
            self.observer_online = False
            if status != 0:
                raise RuntimeError(f'Committee final audit failed: {status}')
            audit_process = self.start([str(self.args.build.resolve() / 'extrachain-dag-audit'),
                                        str(self.home(7) / 'data'), 'joiner'],
                                       self.work / 'observer-audit.log', self.work, self.environment)
            if audit_process.wait(timeout=self.args.audit_timeout) != 0:
                raise RuntimeError('Observer durable DAG and native reward audit failed')
            receipts = audit(self.work, self.waves * self.args.senders * self.args.per_sender,
                             self.submissions, self.cursors, 8)
            files = audit_waves(self.work, self.waves, 8, self.args.file_bytes)
            (self.work / 'durable-wave-receipts.json').write_text(json.dumps(receipts, indent=2))
            (self.work / 'durable-wave-files.json').write_text(json.dumps(files, indent=2))
            if not receipts['complete'] or not files['complete']:
                raise RuntimeError('Final wave artifacts are incomplete')
            self.event('PASS', elapsed_s=elapsed, waves=self.waves, receipts=receipts['expected'],
                       file_copies=files['copies'])
            return 0
        except Exception as error:
            self.event('FAIL', error=str(error))
            return 1
        finally:
            for index in self.stopped:
                try:
                    self.send(index, signal.SIGCONT)
                except (OSError, RuntimeError):
                    pass
            for scope in ('clsact', 'root'):
                subprocess.run(['tc', 'qdisc', 'del', 'dev', 'lo', scope], capture_output=True)
            for child in [self.harness, *self.children]:
                if child.poll() is None:
                    child.terminate()
                try:
                    child.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    child.kill()
                    child.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('work', 'build', 'seed'):
        parser.add_argument(f'--{name}', type=Path, required=True)
    parser.add_argument('--duration', type=int, default=21600)
    parser.add_argument('--interval', type=int, default=300)
    parser.add_argument('--recovery', type=int, default=300)
    parser.add_argument('--audit-timeout', type=int, default=180,
                        help='Time limit in seconds for each offline audit phase')
    parser.add_argument('--senders', type=int, default=6)
    parser.add_argument('--per-sender', type=int, default=48)
    parser.add_argument('--file-bytes', type=int, default=1048576)
    parser.add_argument('--port', type=int, default=25040)
    parser.add_argument('--topology', choices=('mesh', 'ring', 'chain', 'degree3'), default='mesh')
    parser.add_argument('--parent-netns', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if (not 1024 <= args.port <= 65495 or not 60 <= args.interval <= 3600
            or args.duration % args.interval != 0 or not 4 <= args.duration // args.interval <= 256
            or not 1 <= args.senders <= 6 or not 1 <= args.per_sender <= 64
            or not 60 <= args.recovery <= 900 or not 180 <= args.audit_timeout <= 7200
            or not 1 <= args.file_bytes <= 16777216):
        parser.error('Invalid port, duration, interval, recovery, audit or workload bounds')
    if args.parent_netns is None:
        return subprocess.call(['unshare', '-Urn', sys.executable, str(Path(__file__).resolve()),
                                *sys.argv[1:], '--parent-netns', os.readlink('/proc/self/ns/net')])
    return Endurance(args).run()


if __name__ == '__main__':
    raise SystemExit(main())
