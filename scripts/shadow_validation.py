#!/usr/bin/env python3
"""Run the five clean profiles or the forty bounded fault profiles.

Use a frozen harness directory and binary set. Evidence directories are unique;
the scheduler stops adding work after a failure and saves each completed result.
"""
import argparse
import concurrent.futures
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def run_profile(args, profile):
    work = args.work / profile['name']
    started = time.monotonic()
    environment = dict(os.environ, EXTRACHAIN_TEST_BUILD=str(args.build), EXC_SHADOW_WORK=str(work),
                       EXC_SHADOW_SENDERS='6', EXC_SHADOW_PER_SENDER='48', EXC_SHADOW_NODE_COUNT='7',
                       EXC_SHADOW_ALLOWED_DEAD='0', EXC_SHADOW_DFS_BYTES='4194304',
                       EXC_SHADOW_TOPOLOGY=profile['topology'], EXC_SHADOW_RECEIPTS_PYTHON=sys.executable)
    if args.round == 1:
        environment.update(EXC_SHADOW_RUN_SECONDS='360', EXC_SHADOW_DEADLINE_S='330',
                           EXC_SHADOW_MIN_RUNTIME_S='300', EXC_SHADOW_EXTERNAL_CONTROL='0')
        command = ['bash', str(args.harness / 'scripts/shadow_soak.sh'), str(args.seed),
                   str(args.port + profile['index'] * 100)]
    else:
        command = [sys.executable, str(args.harness / 'scripts/shadow_fault_cycle.py'),
                   '--kind', profile['kind'], '--iteration', str(profile['iteration']),
                   '--work', str(work), '--build', str(args.build), '--seed', str(args.seed),
                   '--topology', profile['topology'], '--port', str(args.port)]
    with (args.work / f'{profile["name"]}.log').open('w') as log:
        result = subprocess.run(command, env=environment, stdout=log, stderr=subprocess.STDOUT)
    return dict(profile, status=result.returncode, elapsed_s=round(time.monotonic() - started, 3), work=str(work))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--round', type=int, choices=(1, 2), required=True)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--seed', type=Path, required=True)
    parser.add_argument('--harness', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--jobs', type=int, choices=(1, 2, 3, 4), default=2)
    parser.add_argument('--port', type=int, default=23040)
    args = parser.parse_args()
    if not 1024 <= args.port <= 64995:
        parser.error('--port must be between 1024 and 64995')
    for name in ('work', 'build', 'seed', 'harness'):
        setattr(args, name, getattr(args, name).resolve())
    args.work.mkdir(parents=True, exist_ok=False)
    if args.round == 1:
        profiles = [dict(index=index, name=f'{index:02}-{topology}', topology=topology)
                    for index, topology in enumerate(('mesh', 'ring', 'chain', 'degree3', 'mesh'), 1)]
    else:
        profiles = [dict(index=group * 8 + iteration + 1, name=f'{kind}-{iteration}', kind=kind,
                         iteration=iteration, topology=('mesh', 'ring', 'chain', 'degree3')[iteration % 4])
                    for group, kind in enumerate(('freeze', 'kill-leader', 'restart', 'partition', 'mixed'))
                    for iteration in range(8)]
    (args.work / 'plan.json').write_text(json.dumps(profiles, indent=2) + '\n')
    results = []
    remaining = iter(profiles)
    failed = False
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        pending = {pool.submit(run_profile, args, next(remaining)): None for _ in range(args.jobs)}
        while pending:
            done, _ = concurrent.futures.wait(pending, return_when=concurrent.futures.FIRST_COMPLETED)
            for future in done:
                pending.pop(future)
                result = future.result()
                results.append(result)
                failed = failed or result['status'] != 0
                print(json.dumps(result), flush=True)
                (args.work / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            while not failed and len(pending) < args.jobs:
                profile = next(remaining, None)
                if profile is None:
                    break
                pending[pool.submit(run_profile, args, profile)] = None
    complete = not failed and len(results) == len(profiles)
    print(json.dumps(dict(round=args.round, complete=complete, passed=sum(r['status'] == 0 for r in results),
                          expected=len(profiles))), flush=True)
    return 0 if complete else 1


if __name__ == '__main__':
    raise SystemExit(main())
