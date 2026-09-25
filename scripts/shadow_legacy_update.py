#!/usr/bin/env python3
"""Keep an old node connected for data access, then update the same data directory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time


DOCUMENTS = ('validator-set', 'governance-policy', 'recovery-policy', 'trust-anchor',
             'activation-manifest', 'shadow-config')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--old-bin', type=Path, required=True)
    parser.add_argument('--seed', type=Path, required=True)
    parser.add_argument('--port', type=int, default=24740)
    args = parser.parse_args()
    work, build, seed, old_binary = (p.resolve() for p in (args.work, args.build, args.seed, args.old_bin))
    if not 1024 <= args.port <= 65400 or not old_binary.is_file():
        parser.error('A valid port range and old node binary are required')
    scripts = args.source.resolve() / 'scripts'
    sys.path.insert(0, str(scripts))
    from shadow_receipts import audit
    from shadow_artifacts import audit_files

    work.mkdir(parents=True, exist_ok=False)
    stand = work / 'stand'
    barrier = stand / 'barrier'
    processes, streams, events = [], [], []
    environment = dict(os.environ, EXTRACHAIN_TEST_BUILD=str(build), EXC_SHADOW_WORK=str(stand),
                       EXC_SHADOW_EXTERNAL_CONTROL='1', EXC_SHADOW_SENDERS='2',
                       EXC_SHADOW_PER_SENDER='32', EXC_SHADOW_NODE_COUNT='7',
                       EXC_SHADOW_ALLOWED_DEAD='0', EXC_SHADOW_DFS_BYTES='1048576',
                       EXC_SHADOW_VECTOR_ROWS='48', EXC_SHADOW_VECTOR_CROSS='8',
                       EXC_SHADOW_HISTORY_ROWS='130', EXC_SHADOW_REMOVE_AFTER_S='30',
                       EXC_SHADOW_DFS_MODE='full', EXC_SHADOW_MINING_TEST='1',
                       EXC_SHADOW_RUN_SECONDS='900', EXC_SHADOW_DEADLINE_S='720',
                       EXC_SHADOW_MIN_RUNTIME_S='0', EXC_SHADOW_RECEIPTS_PYTHON=sys.executable)
    for key in ('EXC_SHADOW_WAVES', 'EXC_SHADOW_RESUME_WAVE', 'EXC_SHADOW_OLD_INDEXES',
                'EXC_SHADOW_MINING_LONG_TEST'):
        environment.pop(key, None)

    def event(name, **values):
        events.append(dict(event=name, monotonic_s=time.monotonic(), **values))
        (work / 'events.json').write_text(json.dumps(events, indent=2) + '\n')
        print(name, values, flush=True)

    def start(command, log, cwd=None, env=None):
        stream = log.open('ab')
        streams.append(stream)
        process = subprocess.Popen(command, cwd=cwd, env=env or environment,
                                   stdout=stream, stderr=subprocess.STDOUT)
        processes.append(process)
        return process

    def text(path):
        return path.read_text(errors='replace') if path.exists() else ''

    def wait_for(predicate, seconds, label, process=None):
        end = time.monotonic() + seconds
        while not predicate():
            if soak.poll() is not None:
                raise RuntimeError(f'Committee stopped during {label}')
            if process is not None and process.poll() is not None:
                raise RuntimeError(f'Node exited with {process.returncode} during {label}')
            if time.monotonic() >= end:
                raise TimeoutError(label)
            time.sleep(0.25)

    try:
        for label, binary in (('old', old_binary), ('current', build / 'extrachain-node-run')):
            with binary.open('rb') as stream:
                event('binary', variant=label, path=str(binary), sha256=hashlib.file_digest(stream, 'sha256').hexdigest())
        soak = start(['bash', str(scripts / 'shadow_soak.sh'), str(seed), str(args.port)], work / 'soak.log')
        wait_for(lambda: (barrier / 'committee-ready').exists(), 300, 'committee preparation')
        parent = stand / 'bootstrap/client7'
        home = parent / 'data'
        home.mkdir(parents=True, exist_ok=False)
        event('old-home', copied_dag=False, copied_dfs=False, copied_consensus=False)
        (barrier / 'controller-ready').touch()
        wait_for(lambda: (barrier / 'go').exists(), 30, 'start barrier')
        bootstrap = text(stand / 'bootstrap/server/console.log')
        matches = re.findall(r'DFS payload owner=([0-9a-f]{40}) file_id=([0-9a-f]+) size=(\d+)', bootstrap)
        if len(matches) != 1:
            raise RuntimeError('Expected one bootstrap file for update delivery')
        owner, file_id, size_text = matches[0]
        size = int(size_text)
        pattern = bytes((offset * 131 + 17) & 255 for offset in range(256))
        expected = hashlib.sha256((pattern * ((size + 255) // 256))[:size]).digest()
        join_env = dict(environment, EXC_BIND_IP='127.0.0.8', EXC_DFS_BYTES='0',
                        EXC_DFS_HISTORY_ROWS='0', EXC_DFS_VECTOR_ROWS='0', EXC_DFS_VECTOR_CROSS='0',
                        EXC_DFS_REMOVE_AFTER_S='0', EXC_DFS_MODE='full')
        join_env.pop('EXC_FUND_NODES', None)
        old = start([str(old_binary), 'join', 'data', '127.0.0.1', '9223372036854775807',
                     str(args.port + 27), str(args.port + 20), owner, 'combined-network.bin', str(size)],
                    work / 'old-node.log', cwd=parent, env=join_env)
        wait_for(lambda: re.search(r'conns=[1-9]', text(work / 'old-node.log')), 90, 'old node connection')
        wait_for(lambda: 'restricted data access; Shadow and rewards disabled' in text(stand / 'node-0.log'),
                 90, 'old peer restricted access')

        def transferred(directory=home):
            path = directory / 'dfs' / owner / file_id
            if path.is_symlink() or not path.is_file() or path.stat().st_size != size:
                return False
            with path.open('rb') as stream:
                return hashlib.file_digest(stream, 'sha256').digest() == expected

        wait_for(transferred, 120, 'file delivery to the old node')
        wait_for(lambda: re.search(r'committee node=0 conns=7 shadow_peers=6 ', text(stand / 'node-0.log')),
                 120, 'old peer excluded from Shadow')
        event('restricted-access', connected=True, shadow_peers=6, file_bytes=size)
        # Pause the current nodes so the old client cannot download from them directly.
        paused = []
        client = None
        try:
            for index in range(7):
                marker = barrier / f'pid-{index}'
                if not marker.exists():
                    marker = barrier / f'initial-pid-{index}'
                pid = int(marker.read_text())
                if pid <= 1 or not Path(f'/proc/{pid}/cwd').resolve().is_relative_to(stand):
                    raise RuntimeError('Committee PID does not belong to this stand')
                os.kill(pid, signal.SIGSTOP)
                paused.append(pid)
            wait_for(lambda: all(Path(f'/proc/{pid}/stat').read_text().rsplit(')', 1)[1].split()[0] == 'T'
                                 for pid in paused), 5, 'committee pause')
            client_parent = work / 'old-client'
            client_parent.mkdir(exist_ok=False)
            client_home = client_parent / 'data'
            client = start([str(old_binary), 'join', 'data', '127.0.0.8', '9223372036854775807',
                            str(args.port + 28), str(args.port + 27), owner, 'combined-network.bin', str(size)],
                           work / 'old-client.log', cwd=client_parent,
                           env=dict(join_env, EXC_BIND_IP='127.0.0.9'))
            wait_for(lambda: transferred(client_home), 90, 'old client file delivery through old node', client)
            event('old-client-read', file_bytes=size, current_nodes_paused=len(paused))
        finally:
            for pid in paused:
                try:
                    os.kill(pid, signal.SIGCONT)
                except ProcessLookupError:
                    pass
            if client is not None and client.poll() is None:
                client.terminate()
                try:
                    client.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    client.kill()
                    client.wait(timeout=10)
        if old.poll() is None:
            old.terminate()
            try:
                old.wait(timeout=15)
            except subprocess.TimeoutExpired:
                old.kill()
                old.wait(timeout=15)
        (home / 'consensus').mkdir(exist_ok=True)
        for name in DOCUMENTS:
            shutil.copy2(stand / 'bootstrap/server/data/consensus' / f'{name}.msgpack',
                         home / 'consensus' / f'{name}.msgpack')
        event('update', same_home=str(home), copied_documents=list(DOCUMENTS), replaced_data=False)
        updated = start([str(build / 'extrachain-node-run'), 'committee', 'data', 'joiner', '7',
                         str(args.port + 27), str(args.port + 20), '8', '0', '600', str(barrier), '1', '1'],
                        stand / 'node-7.log', cwd=parent, env=join_env)
        wait_for(lambda: re.search(r'committee node=0 conns=7 shadow_peers=7 ', text(stand / 'node-0.log')),
                 90, 'current signed peer access after update', updated)
        submissions, cursors, cache = {}, {}, {}

        def converged():
            if updated.poll() is not None:
                raise RuntimeError('Updated node stopped before convergence')
            receipts = audit(stand, 64, submissions, cursors, 8)
            files = audit_files(stand, 8, size=1048576, cache=cache, publishers=7)
            (work / 'receipts.json').write_text(json.dumps(receipts, indent=2))
            (work / 'files.json').write_text(json.dumps(files, indent=2))
            return receipts['complete'] and files['complete']

        wait_for(converged, 240, 'updated node data convergence')
        event('all-eight-converged', receipts=64, file_copies=56)
        (barrier / 'faults-done').touch()
        if soak.wait(timeout=240) != 0:
            raise RuntimeError('Combined committee audit failed')
        updated.terminate()
        updated.wait(timeout=30)
        checked = start([str(build / 'extrachain-dag-audit'), str(home), 'joiner'],
                        work / 'updated-node-audit.log', env=join_env)
        if checked.wait(timeout=180) != 0:
            raise RuntimeError('Updated node durable DAG and mining audit failed')
        event('PASS')
        return 0
    except Exception as error:
        event('FAIL', error=str(error))
        return 1
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
        for process in reversed(processes):
            try:
                process.wait(timeout=20)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
        for stream in streams:
            stream.close()


if __name__ == '__main__':
    raise SystemExit(main())
