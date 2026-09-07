#!/usr/bin/env python3
"""Run five alternating baseline/candidate pairs from one prepared committee.

Warm up each build first. Every stand restores the same seven identities and
initial data at the same absolute path. Archive a stand only after its children
stop and all offline checks finish. Run this controller on an otherwise idle host.
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import sys


def summarize_comparison(measurements):
    if len(measurements) != 5:
        raise ValueError("Five complete pairs are required")
    if not all(pair[build]["complete"] for pair in measurements.values()
               for build in ("baseline", "candidate")):
        raise ValueError("Every stand must pass its data checks")
    ratios = [pair['baseline']['latency_ms'] / pair['candidate']['latency_ms']
              for pair in measurements.values()]
    resource_checks = {}
    for metric in ('cpu_s', 'peak_rss_bytes', 'read_bytes', 'write_bytes', 'dfs_from_barrier_s'):
        regressed = [number for number, pair in measurements.items()
                     if pair['candidate'][metric] > pair['baseline'][metric] * 1.05]
        resource_checks[metric] = {'regressed_pairs': regressed, 'pass': len(regressed) < 3}
    result = {'complete': True, 'pairs': measurements, 'throughput_ratios': ratios,
              'median_throughput_ratio': statistics.median(ratios),
              'all_pairs_at_least_1_5': all(ratio >= 1.5 for ratio in ratios),
              'resource_checks': resource_checks}
    medians = {build: statistics.median(pair[build]['intents_per_second']
                                      for pair in measurements.values())
               for build in ('baseline', 'candidate')}
    result['median_intents_per_second'] = medians
    result['median_rate_ratio'] = medians['candidate'] / medians['baseline']
    result['target_met'] = result['median_rate_ratio'] >= 1.5 and all(
        check['pass'] for check in resource_checks.values())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work', type=Path, required=True)
    parser.add_argument('--baseline', type=Path, required=True)
    parser.add_argument('--candidate', type=Path, help='Omit to establish the initial baseline only')
    parser.add_argument('--seed', type=Path, required=True)
    parser.add_argument('--harness', type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument('--port', type=int, default=37040)
    parser.add_argument('--warm-executable', action='store_true',
                        help='Use the same warm executable cache condition for every stand')
    parser.add_argument('--cold-dag-packs', action='store_true',
                        help='Request a cold immutable DAG pack cache in every stand')
    args = parser.parse_args()
    for name in ('work', 'baseline', 'candidate', 'seed', 'harness'):
        if getattr(args, name) is not None:
            setattr(args, name, getattr(args, name).resolve())
    args.work.mkdir(parents=True, exist_ok=False)
    live = args.work / 'stand'
    fixture = args.work / 'fixture'
    measurements = {}
    schedule = [(0, 'baseline')]
    if args.candidate:
        schedule.append((0, 'candidate'))
    for pair in range(1, 6):
        order = ('baseline', 'candidate') if pair % 2 else ('candidate', 'baseline')
        if args.candidate is None:
            order = ('baseline',)
        schedule.extend((pair, build) for build in order)
    for index, (pair, build) in enumerate(schedule):
        name = f'{pair:02}-{build}'
        command = [sys.executable, str(args.harness / 'scripts/shadow_benchmark.py'),
                   '--work', str(live), '--build', str(getattr(args, build)), '--seed', str(args.seed),
                   '--harness', str(args.harness), '--port', str(args.port),
                   '--save-fixture' if index == 0 else '--fixture', str(fixture)]
        if args.warm_executable:
            command.append('--warm-executable')
        if args.cold_dag_packs:
            command.append('--cold-dag-packs')
        with (args.work / f'{name}.log').open('w') as log:
            status = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT).returncode
        if status != 0:
            print(json.dumps({'complete': False, 'failed': name, 'status': status}), flush=True)
            return 1
        measurement = json.loads((live / 'measurement.json').read_text())
        if not measurement['complete']:
            raise RuntimeError(f'Incomplete measurement: {name}')
        live.rename(args.work / name)
        if pair:
            measurements.setdefault(pair, {})[build] = measurement
        print(json.dumps({'stand': name, 'latency_ms': measurement['latency_ms']}), flush=True)
    if args.candidate is None:
        result = {'complete': True, 'measurements': measurements,
                  'executable_cache': 'warm' if args.warm_executable else 'unchanged',
                  'dag_pack_cache': 'discard-requested' if args.cold_dag_packs else 'unchanged', 'medians': {
            metric: statistics.median(pair['baseline'][metric] for pair in measurements.values())
            for metric in ('latency_ms', 'intents_per_second', 'cpu_s', 'peak_rss_bytes',
                           'read_bytes', 'write_bytes', 'dfs_from_barrier_s')}}
        (args.work / 'baseline.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result['medians'], indent=2))
        return 0
    result = summarize_comparison(measurements)
    result['executable_cache'] = 'warm' if args.warm_executable else 'unchanged'
    result['dag_pack_cache'] = 'discard-requested' if args.cold_dag_packs else 'unchanged'
    (args.work / 'comparison.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'pairs'}, indent=2))
    return 0 if result['target_met'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
