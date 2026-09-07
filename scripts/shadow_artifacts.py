#!/usr/bin/env python3
"""Check every deterministic workload file on every member after a Shadow stand."""
import argparse
import functools
import hashlib
import json
from pathlib import Path
import re


PUBLISHED = re.compile(r'^\[node-run\] DFS stored owner=([0-9a-f]{40}) file_id=([0-9a-f]+) size=(\d+)$', re.M)
ANSI_SGR = re.compile(r'\x1b\[[0-9;]*m')


@functools.lru_cache(maxsize=8)
def expected_digest(publisher, size):
    pattern = bytes((offset * (131 + publisher) + 17 + publisher * 7) & 255 for offset in range(256))
    return hashlib.sha256((pattern * ((size + 255) // 256))[:size]).digest()


def homes(work, nodes):
    return [work / 'bootstrap' / ('server' if node == 0 else f'client{node}') / 'data'
            for node in range(nodes)]


def audit_files(work, nodes=7, size=4194304, cache=None, publishers=None):
    publishers = nodes if publishers is None else publishers
    if not 1 <= publishers <= nodes:
        raise ValueError("Publishers must be in 1..nodes")
    cache = {} if cache is None else cache
    directories = homes(work, nodes)
    failures = []
    copies = 0
    for publisher in range(publishers):
        log = work / f'node-{publisher}.log'
        entries = PUBLISHED.findall(ANSI_SGR.sub('', log.read_text(errors='replace'))) if log.exists() else []
        if len(entries) != 1 or int(entries[0][2]) != size:
            failures.append(f'Publisher {publisher}: expected one file of {size} bytes')
            continue
        owner, file_id, _ = entries[0]
        expected = expected_digest(publisher, size)
        for member, directory in enumerate(directories):
            path = directory / 'dfs' / owner / file_id
            try:
                stat = path.stat()
                if stat.st_size != size:
                    raise ValueError(f'size {stat.st_size}')
                key = (path, stat.st_ino, stat.st_size, stat.st_mtime_ns)
                digest = cache.get(key)
                if digest is None:
                    with path.open('rb') as stream:
                        digest = hashlib.file_digest(stream, 'sha256').digest()
                    if digest == expected:
                        cache[key] = digest
                if digest != expected:
                    raise ValueError('content mismatch')
                copies += 1
            except (OSError, ValueError) as error:
                failures.append(f'Member {member}, publisher {publisher}: {error}')
    return {'complete': not failures and copies == nodes * publishers, 'copies': copies,
            'expected_copies': nodes * publishers, 'bytes_per_file': size, 'failures': failures}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('work', type=Path)
    parser.add_argument('--nodes', type=int, choices=(7, 8), default=7)
    parser.add_argument('--size', type=int, default=4194304)
    parser.add_argument('--publishers', type=int, help='Existing publishers; defaults to all nodes')
    args = parser.parse_args()
    if args.size <= 0:
        parser.error('--size must be positive')
    if args.publishers is not None and not 1 <= args.publishers <= args.nodes:
        parser.error('--publishers must be in 1..nodes')
    result = audit_files(args.work.resolve(), args.nodes, args.size, publishers=args.publishers)
    print(json.dumps(result, indent=2))
    return 0 if result['complete'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
