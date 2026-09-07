#!/usr/bin/env python3
"""Check each submitted workload intent in every node's durable receipt store.

Requires msgpack. Use --watch during a stand to measure durable completion.
The clock is the Linux monotonic clock used by the local node runner.
"""
import argparse
from contextlib import closing
import base64
import json
import re
import sqlite3
import time
from pathlib import Path

import msgpack


SUBMISSION = re.compile(r'intent hash=([0-9a-f]{64}) submitted_at_ms=(\d+)')


def decode_store_payload(payload):
    return msgpack.unpackb(base64.b64decode(payload + '=' * (-len(payload) % 4),
                                          altchars=b'-_', validate=True), raw=False)


def audit(work, expected, submissions, cursors, node_count):
    for index in range(node_count):
        log = work / f'node-{index}.log'
        if log.exists():
            position, pending = cursors.get(index, (0, ''))
            if log.stat().st_size < position:
                position, pending = 0, ''
            with log.open(errors='replace') as stream:
                stream.seek(position)
                text = pending + stream.read()
                position = stream.tell()
            complete, separator, pending = text.rpartition('\n')
            if not separator:
                complete, pending = '', text
            for intent, timestamp in SUBMISSION.findall(complete):
                submissions.setdefault(intent, int(timestamp))
            cursors[index] = position, pending
    result = {'submitted': len(submissions), 'expected': expected, 'nodes': [], 'complete': False}
    reference = None
    for index in range(node_count):
        home = work / 'bootstrap' / ('server' if index == 0 else f'client{index}') / 'data'
        databases = list(home.rglob('intent-pool.sqlite'))
        finalized = {}
        error = None
        if len(databases) == 1:
            try:
                with closing(sqlite3.connect(f'{databases[0].as_uri()}?mode=ro', uri=True, timeout=1)) as database:
                    for intent, payload in database.execute('SELECT hash, payload FROM consensus_intent_receipts'):
                        if intent not in submissions:
                            continue
                        receipt = decode_store_payload(payload)
                        if (isinstance(receipt, list) and len(receipt) == 6 and receipt[0] == intent
                                and receipt[1] == 2 and receipt[2] > 0 and receipt[3] > 0):
                            finalized[intent] = receipt
            except (sqlite3.Error, ValueError, TypeError) as exception:
                error = str(exception)
        else:
            error = f'Expected one receipt database, found {len(databases)}'
        same = reference is None or finalized == reference
        if reference is None:
            reference = finalized
        result['nodes'].append({'index': index, 'finalized': len(finalized), 'same_receipts': same, 'error': error})
    result['complete'] = (len(submissions) == expected and all(
        node['finalized'] == expected and node['same_receipts'] and node['error'] is None
        for node in result['nodes']))
    if result['complete']:
        result['elapsed_ms'] = time.monotonic_ns() // 1_000_000 - min(submissions.values())
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('work', type=Path)
    parser.add_argument('--nodes', type=int, choices=(7, 8), default=7)
    parser.add_argument('--expected', type=int, default=288)
    parser.add_argument('--watch', type=float, default=0, help='Maximum wait in seconds')
    args = parser.parse_args()
    if args.expected <= 0 or args.watch < 0:
        parser.error('--expected must be positive and --watch must be nonnegative')
    work = args.work.resolve()
    deadline = time.monotonic() + args.watch
    submissions = {}
    cursors = {}
    while True:
        result = audit(work, args.expected, submissions, cursors, args.nodes)
        if result['complete'] or time.monotonic() >= deadline:
            result['measured_live'] = args.watch > 0
            if args.watch <= 0:
                result.pop('elapsed_ms', None)
            print(json.dumps(result, indent=2))
            return 0 if result['complete'] else 1
        time.sleep(0.2)


if __name__ == '__main__':
    raise SystemExit(main())
