#!/usr/bin/env python3
"""Measure a seven-member 288-intent workload with seven concurrent 4 MiB files.

Run one stand at a time on an otherwise idle machine. The soak harness supplies
the start barrier and the final offline checks. CPU and I/O cover the interval
from barrier release through receipt and file completion, including funding.
The primary latency starts at the first accepted workload intent. All metrics
use the same 100 ms polling interval for baseline and candidate builds.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from shadow_artifacts import audit_files
from shadow_receipts import audit


def warm_executable(path):
    started = time.monotonic()
    digest = hashlib.sha256()
    size = 0
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
            size += len(chunk)
    return {'path': str(path), 'bytes': size, 'sha256': digest.hexdigest(),
            'elapsed_s': time.monotonic() - started}


def discard_dag_pack_cache(work):
    if not hasattr(os, 'posix_fadvise') or not hasattr(os, 'POSIX_FADV_DONTNEED'):
        raise RuntimeError('Cold DAG packs require POSIX_FADV_DONTNEED')
    started = time.monotonic()
    paths = sorted((work / 'bootstrap').glob('*/data/dag/packs/*.pack'))
    if not paths:
        raise RuntimeError('No DAG packs found for cache control')
    files = []
    for path in paths:
        if path.is_symlink() or not path.resolve().is_relative_to(work.resolve()):
            raise RuntimeError('A DAG pack is outside the current stand')
        with path.open('rb') as stream:
            os.fsync(stream.fileno())
            os.posix_fadvise(stream.fileno(), 0, 0, os.POSIX_FADV_DONTNEED)
            files.append({'path': str(path), 'bytes': os.fstat(stream.fileno()).st_size})
    return {'advice': 'POSIX_FADV_DONTNEED', 'files': files,
            'elapsed_s': time.monotonic() - started}


def resources(pids):
    totals = dict(cpu_ticks=0, rss_bytes=0, read_bytes=0, write_bytes=0)
    for pid in pids:
        directory = Path('/proc') / str(pid)
        stat = (directory / 'stat').read_text().rsplit(')', 1)[1].split()
        if stat[0] == 'Z':
            raise RuntimeError(f'Member process {pid} exited during measurement')
        totals['cpu_ticks'] += int(stat[11]) + int(stat[12])
        totals['rss_bytes'] += int(stat[21]) * os.sysconf('SC_PAGE_SIZE')
        io = dict(line.split(':', 1) for line in (directory / 'io').read_text().splitlines())
        totals['read_bytes'] += int(io['read_bytes'])
        totals['write_bytes'] += int(io['write_bytes'])
    return totals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--seed', type=Path, required=True)
    parser.add_argument('--harness', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--port', type=int, default=37040)
    parser.add_argument('--warm-executable', action='store_true',
                        help='Read the node executable into page cache before releasing the measurement barrier')
    parser.add_argument('--cold-dag-packs', action='store_true',
                        help='Discard clean cache pages of this stand\'s immutable DAG packs before measurement')
    parser.add_argument('--topology', choices=('mesh', 'ring', 'chain', 'degree3'), default='mesh')
    fixture = parser.add_mutually_exclusive_group()
    fixture.add_argument('--fixture', type=Path)
    fixture.add_argument('--save-fixture', type=Path)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65500:
        parser.error('--port must be between 1024 and 65500')
    for name in ('work', 'build', 'seed', 'harness'):
        setattr(args, name, getattr(args, name).resolve())
    args.work.mkdir(parents=True, exist_ok=False)
    barrier = args.work / 'barrier'
    environment = dict(os.environ, EXTRACHAIN_TEST_BUILD=str(args.build), EXC_SHADOW_WORK=str(args.work),
                       EXC_SHADOW_SENDERS='6', EXC_SHADOW_PER_SENDER='48', EXC_SHADOW_NODE_COUNT='7',
                       EXC_SHADOW_ALLOWED_DEAD='0', EXC_SHADOW_DFS_BYTES='4194304',
                       EXC_SHADOW_TOPOLOGY=args.topology, EXC_SHADOW_RECEIPTS_PYTHON=sys.executable,
                       EXC_SHADOW_RUN_SECONDS='360', EXC_SHADOW_DEADLINE_S='300',
                       EXC_SHADOW_MIN_RUNTIME_S='0', EXC_SHADOW_EXTERNAL_CONTROL='1')
    environment.pop('EXC_SHADOW_PREPARED_FIXTURE', None)
    environment.pop('EXC_SHADOW_SAVE_FIXTURE', None)
    if args.fixture:
        environment['EXC_SHADOW_PREPARED_FIXTURE'] = str(args.fixture.resolve())
    if args.save_fixture:
        environment['EXC_SHADOW_SAVE_FIXTURE'] = str(args.save_fixture.resolve())
    result = {'complete': False, 'build': str(args.build), 'topology': args.topology,
              'poll_interval_ms': 100, 'expected_intents': 288, 'expected_files': 7}
    with (args.work / 'stand.log').open('w') as log:
        process = subprocess.Popen(['bash', str(args.harness / 'scripts/shadow_soak.sh'),
                                    str(args.seed), str(args.port)], env=environment,
                                   stdout=log, stderr=subprocess.STDOUT)
        try:
            startup_deadline = time.monotonic() + 180
            while not (barrier / 'committee-ready').exists():
                if process.poll() is not None or time.monotonic() >= startup_deadline:
                    raise RuntimeError('Committee did not reach the start barrier')
                time.sleep(0.1)
            pids = [int((barrier / f'initial-pid-{node}').read_text()) for node in range(7)]
            if args.warm_executable:
                result['executable_preload'] = warm_executable(args.build / 'extrachain-node-run')
            if args.cold_dag_packs:
                result['dag_pack_cache_advice'] = discard_dag_pack_cache(args.work)
            before = resources(pids)
            peak_rss = before['rss_bytes']
            started = time.monotonic()
            (barrier / 'controller-ready').touch()
            submissions, cursors, file_cache = {}, {}, {}
            receipt_result = file_result = None
            with (args.work / 'resources.jsonl').open('w') as trace:
                while time.monotonic() - started < 300:
                    if process.poll() is not None:
                        raise RuntimeError('Stand exited before workload completion')
                    current = resources(pids)
                    peak_rss = max(peak_rss, current['rss_bytes'])
                    trace.write(json.dumps(dict(current, elapsed_s=time.monotonic() - started)) + '\n')
                    if receipt_result is None:
                        check = audit(args.work, 288, submissions, cursors, 7)
                        if check['complete']:
                            receipt_result = check
                    if file_result is None:
                        check = audit_files(args.work, cache=file_cache)
                        if check['complete']:
                            file_result = dict(check, elapsed_s=time.monotonic() - started)
                    if receipt_result is not None and file_result is not None:
                        current = resources(pids)
                        peak_rss = max(peak_rss, current['rss_bytes'])
                        result.update(latency_ms=receipt_result['elapsed_ms'],
                                      intents_per_second=288000 / receipt_result['elapsed_ms'],
                                      dfs_from_barrier_s=file_result['elapsed_s'],
                                      cpu_s=(current['cpu_ticks'] - before['cpu_ticks']) / os.sysconf('SC_CLK_TCK'),
                                      peak_rss_bytes=peak_rss,
                                      read_bytes=current['read_bytes'] - before['read_bytes'],
                                      write_bytes=current['write_bytes'] - before['write_bytes'])
                        break
                    time.sleep(0.1)
                else:
                    raise RuntimeError('Workload did not complete within 300 seconds')
            (barrier / 'faults-done').touch()
            result['stand_status'] = process.wait(timeout=120)
            result['receipts'] = audit(args.work, 288, {}, {}, 7)
            result['receipts'].pop('elapsed_ms', None)
            result['files'] = audit_files(args.work)
            result['complete'] = (result['stand_status'] == 0 and result['receipts']['complete']
                                  and result['files']['complete'])
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            result['error'] = str(error)
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            (args.work / 'measurement.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
