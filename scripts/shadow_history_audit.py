#!/usr/bin/env python3
"""Verify signed-history heads and collection projections across a combined stand."""

import hashlib
from pathlib import Path
import re
import sqlite3
import sys

PUBLICATION = re.compile(
    r"\[node-run\] DFS history owner=([0-9a-f]{40}) file_id=([0-9a-f]{64}) rows=(\d+) hash=([0-9a-f]{64})"
)


def inspect(path, expected_rows, expected_hash):
    source = sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True)
    snapshot = sqlite3.connect(":memory:")
    try:
        source.backup(snapshot)
        rows = snapshot.execute("SELECT * FROM HistoryItems ORDER BY id").fetchall()
        head = snapshot.execute("SELECT hash FROM historical_chain ORDER BY id DESC LIMIT 1").fetchone()
        history_count = snapshot.execute("SELECT count(*) FROM historical_chain").fetchone()[0]
        if head != (expected_hash,) or history_count != expected_rows + 4 or len(rows) != expected_rows:
            raise ValueError(f"Incomplete history or projection in {path}")
        changed = snapshot.execute("SELECT payload FROM HistoryItems WHERE id=1").fetchone()
        removed = snapshot.execute("SELECT id FROM HistoryItems WHERE id=2").fetchone()
        if changed != ("updated",) or removed is not None:
            raise ValueError(f"Update or removal is absent in {path}")
        return hashlib.sha256(repr(rows).encode()).hexdigest()
    finally:
        source.close()
        snapshot.close()


def audit(work, nodes, rows, dead):
    live = [index for index in range(nodes) if index not in dead]
    if not live:
        raise ValueError("No live nodes to audit")
    published = []
    for index in live:
        records = set(PUBLICATION.findall((work / f"node-{index}.log").read_text(errors="replace")))
        if len(records) != 1:
            raise ValueError(f"Node {index}: expected one history publication")
        owner, file_id, count, head = records.pop()
        if int(count) != rows - 1:
            raise ValueError(f"Node {index}: incomplete history publication")
        published.append((owner, file_id, head))
    if len({(owner, file_id) for owner, file_id, _ in published}) != len(live):
        raise ValueError("Duplicate history publication")
    reference = {}
    for index in live:
        home = work / "bootstrap" / ("server" if index == 0 else f"client{index}") / "data"
        for owner, file_id, head in published:
            digest = inspect(home / "dfs" / owner / file_id, rows - 1, head)
            if digest != reference.setdefault((owner, file_id), digest):
                raise ValueError(f"Node {index}: collection {file_id} differs")
        print(f"history: node {index} has {len(published)}/{len(published)} equal collections ({rows - 1} rows, {rows + 3} history records each)")


if __name__ == "__main__":
    try:
        audit(Path(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]),
              {int(index) for index in sys.argv[4].split()})
    except (OSError, ValueError, sqlite3.Error) as error:
        print(f"history: {error}")
        sys.exit(1)
