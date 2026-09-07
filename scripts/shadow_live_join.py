#!/usr/bin/env python3
"""Join a fresh observer during live finality, then verify all eight durable stores."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from shadow_artifacts import audit_files
from shadow_receipts import audit


DOCUMENTS = ('activation-manifest', 'governance-policy', 'recovery-policy',
             'shadow-config', 'trust-anchor', 'validator-set')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--seed', type=Path, required=True)
    parser.add_argument('--port', type=int, default=41040)
    args = parser.parse_args()
    work, build, seed = (path.resolve() for path in (args.work, args.build, args.seed))
    work.mkdir(parents=True, exist_ok=False)
    scripts = Path(__file__).resolve().parent
    stand = work / 'stand'
    barrier = stand / 'barrier'
    processes, logs, events = [], [], []
    environment = dict(os.environ, EXTRACHAIN_TEST_BUILD=str(build), EXC_SHADOW_WORK=str(stand),
                       EXC_SHADOW_EXTERNAL_CONTROL='1', EXC_SHADOW_SENDERS='6',
                       EXC_SHADOW_PER_SENDER='48', EXC_SHADOW_NODE_COUNT='7',
                       EXC_SHADOW_ALLOWED_DEAD='0', EXC_SHADOW_DFS_BYTES='4194304',
                       EXC_SHADOW_TOPOLOGY='chain', EXC_SHADOW_RUN_SECONDS='420',
                       EXC_SHADOW_DEADLINE_S='300', EXC_SHADOW_MIN_RUNTIME_S='0',
                       EXC_SHADOW_RECEIPTS_PYTHON=sys.executable)

    def event(name, **values):
        events.append(dict(name=name, monotonic_s=time.monotonic(), **values))
        (work / 'events.json').write_text(json.dumps(events, indent=2) + '\n')
        print(name, values, flush=True)

    def start(command, log, cwd=None, env=None):
        stream = log.open('w')
        logs.append(stream)
        process = subprocess.Popen(command, cwd=cwd, env=env or environment,
                                   stdout=stream, stderr=subprocess.STDOUT)
        processes.append(process)
        return process

    def wait_until(predicate, deadline, process):
        while not predicate():
            if process.poll() is not None:
                raise RuntimeError(f'Process ended before completion: {process.returncode}')
            if time.monotonic() >= deadline:
                raise TimeoutError('Live join deadline exceeded')
            time.sleep(0.1)

    def checked(command, log, env=None):
        process = start(command, log, env=env)
        if process.wait(timeout=180) != 0:
            raise RuntimeError(f'Offline check failed: {log.name}')

    try:
        soak = start(['bash', str(scripts / 'shadow_soak.sh'), str(seed), str(args.port)], work / 'soak.log')
        wait_until(lambda: (barrier / 'committee-ready').exists(), time.monotonic() + 300, soak)
        join_parent = stand / 'bootstrap' / 'client7'
        home = join_parent / 'data'
        home.mkdir(parents=True, exist_ok=False)
        (home / 'consensus').mkdir()
        for document in DOCUMENTS:
            shutil.copy2(stand / 'bootstrap/server/data/consensus' / f'{document}.msgpack',
                         home / 'consensus' / f'{document}.msgpack')
        assert not (home / 'dag').exists()
        assert not (home / 'dfs').exists()
        event('fresh-home', copied_documents=list(DOCUMENTS), copied_dag=False, copied_dfs=False)
        (barrier / 'controller-ready').touch()
        wait_until(lambda: (barrier / 'go').exists(), time.monotonic() + 30, soak)
        deadline = time.monotonic() + 300
        submissions, cursors, file_cache = {}, {}, {}
        wait_until(lambda: audit(stand, 288, submissions, cursors, 7)['submitted'] > 0, deadline, soak)
        before = audit(stand, 288, submissions, cursors, 7)
        if before['complete']:
            raise RuntimeError('Workload completed before the fresh node could join')
        tip = json.loads((stand / 'bootstrap/server/data/dag/range').read_text())['last']
        target = int(tip) + 1
        # The existing join runner creates an identity and pulls a moving DAG.
        # After its first catch-up, the committee runner keeps that identity online
        # for the final receipt and file audit; no preloaded DAG or identity is used.
        event('join-start', committee_tip=int(tip), moving_target=target, submitted=before['submitted'])
        join_env = dict(environment, EXC_BIND_IP='127.0.0.8', EXC_DFS_BYTES='0', EXC_FUND_NODES='')
        join = start([str(build / 'extrachain-node-run'), 'join', 'data', '127.0.0.7',
                      str(target), str(args.port + 27), str(args.port + 26)],
                     work / 'fresh-join.log', cwd=join_parent, env=join_env)
        status = join.wait(timeout=max(1, deadline - time.monotonic()))
        if status:
            raise RuntimeError(f'Fresh join failed: {status}')
        event('initial-catch-up')
        callback = start([str(build / 'extrachain-shadow-bootstrap-tests'),
                          '--callback-bootstrap', str(home)],
                         work / 'callback-bootstrap.log', env=join_env)
        if callback.wait(timeout=max(1, min(180, deadline - time.monotonic()))) != 0:
            raise RuntimeError('Bootstrap admission callback check failed')
        event('bootstrap-callback-complete')
        observer = start([str(build / 'extrachain-node-run'), 'committee', 'data', 'joiner', '7',
                          str(args.port + 27), str(args.port + 20), '8', '0', '360', str(barrier), '1', '1'],
                         stand / 'node-7.log', cwd=join_parent, env=join_env)
        while True:
            receipts = audit(stand, 288, submissions, cursors, 8)
            files = audit_files(stand, 8, cache=file_cache, publishers=7)
            if receipts['complete'] and files['complete']:
                break
            if observer.poll() is not None or soak.poll() is not None:
                (work / 'incomplete-receipts.json').write_text(json.dumps(receipts, indent=2))
                (work / 'incomplete-files.json').write_text(json.dumps(files, indent=2))
                raise RuntimeError('A live process stopped before data convergence')
            if time.monotonic() >= deadline:
                (work / 'incomplete-receipts.json').write_text(json.dumps(receipts, indent=2))
                (work / 'incomplete-files.json').write_text(json.dumps(files, indent=2))
                raise TimeoutError('All-node receipts and DFS did not converge')
            time.sleep(0.5)
        event('all-eight-converged', receipts=288, file_copies=56)
        observer.terminate()
        if observer.wait(timeout=30) != 0:
            raise RuntimeError('Observer shutdown failed')
        (barrier / 'faults-done').touch()
        if soak.wait(timeout=180) != 0:
            raise RuntimeError('Committee final audit failed')
        command = [str(build / 'extrachain-dag-audit'), str(home), 'joiner']
        if os.environ.get('EXC_SHADOW_CAPTURE_AUDIT_CRASH') == '1':
            command = [sys.executable, str(scripts / 'shadow_crash_capture.py'),
                       '--core', str(work / 'joiner-audit.core'), '--', *command]
        checked(command, work / 'joiner-audit.log')
        receipts = audit(stand, 288, submissions, cursors, 8)
        receipts.pop('elapsed_ms', None)
        files = audit_files(stand, 8, publishers=7)
        (work / 'durable-receipts.json').write_text(json.dumps(receipts, indent=2) + '\n')
        (work / 'deterministic-files.json').write_text(json.dumps(files, indent=2) + '\n')
        if not receipts['complete'] or not files['complete']:
            raise RuntimeError('Offline all-node data checks failed')
        (work / 'result.json').write_text(json.dumps({'complete': True, 'nodes': 8,
                                                     'fresh_join': True, 'bootstrap_callback': True, 'file_copies': 56}, indent=2) + '\n')
        event('complete')
        return 0
    except (OSError, RuntimeError, TimeoutError, subprocess.TimeoutExpired) as error:
        event('failed', error=str(error))
        (work / 'result.json').write_text(json.dumps({'complete': False, 'error': str(error)}, indent=2) + '\n')
        return 1
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        for stream in logs:
            stream.close()


if __name__ == '__main__':
    raise SystemExit(main())
