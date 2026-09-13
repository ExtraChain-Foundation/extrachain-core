#!/usr/bin/env python3
"""Check every published vector and compare its rows across the stand."""

import hashlib
from pathlib import Path
import re
import sqlite3
import sys


PUBLICATION = re.compile(
    r"\[node-run\] DFS vector owner=([0-9a-f]{40}) file_id=([0-9a-f]{64}) rows=(\d+)/(\d+)"
)


def row_digest(path):
    source = sqlite3.connect(path.resolve().as_uri() + "?mode=ro", uri=True)
    try:
        source.execute("PRAGMA cache_size=-2048")
        source.execute("BEGIN")
        digest = hashlib.sha256()
        count = 0
        # Derived index and descriptor tables are not vector content.
        for row in source.execute("SELECT * FROM Vector ORDER BY id"):
            digest.update(repr(row).encode("utf-8"))
            digest.update(b"\n")
            count += 1
        payload_bytes = source.execute(
            "SELECT COALESCE(SUM(length(CAST(payload AS BLOB))), 0) FROM Vector"
        ).fetchone()[0]
        return count, digest.hexdigest(), payload_bytes
    finally:
        source.close()


def audit(work, nodes, rows, cross, dead, minimum_payload_bytes=0):
    live = [index for index in range(nodes) if index not in dead]
    if not live:
        raise ValueError("No live nodes to audit")
    published = []
    for index in live:
        records = set()
        with (work / f"node-{index}.log").open(errors="replace") as log:
            for line in log:
                records.update(PUBLICATION.findall(line))
        if len(records) != 1:
            raise ValueError(f"Node {index}: expected one vector publication, found {len(records)}")
        owner, file_id, appended, requested = records.pop()
        if int(appended) != rows or int(requested) != rows:
            raise ValueError(f"Node {index}: incomplete vector publication {appended}/{requested}")
        published.append((owner, file_id))
    if len(set(published)) != len(live):
        raise ValueError("Different publishers reported the same vector")
    expected = rows + cross * (nodes - 1)
    reference = {}
    for index in live:
        home = work / "bootstrap" / ("server" if index == 0 else f"client{index}") / "data"
        for owner, file_id in published:
            count, digest, payload_bytes = row_digest(home / "dfs" / owner / file_id)
            if count != expected:
                raise ValueError(f"Node {index}: vector {file_id} has {count} rows, expected {expected}")
            if payload_bytes < minimum_payload_bytes:
                raise ValueError(f"Node {index}: vector {file_id} has {payload_bytes} payload bytes, "
                                 f"expected at least {minimum_payload_bytes}")
            previous = reference.setdefault((owner, file_id), digest)
            if digest != previous:
                raise ValueError(f"Node {index}: vector {file_id} differs from the other nodes")
        print(f"vectors: node {index} has {len(published)}/{len(published)} complete, equal vectors ({expected} rows each)")


if __name__ == "__main__":
    try:
        audit(Path(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]),
              {int(index) for index in sys.argv[5].split()}, int(sys.argv[6]) if len(sys.argv) > 6 else 0)
    except (OSError, ValueError, sqlite3.Error) as error:
        print(f"vectors: {error}")
        sys.exit(1)
