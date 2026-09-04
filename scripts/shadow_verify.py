#!/usr/bin/env python3
"""Cross-node content verification for a shadow_soak.sh work directory.

The judge of a run is not "did the processes survive" but "do the nodes hold the
same content". Heights converge on different content all the time — we watched
that happen for a whole day — so the invariant is a per-section hash of the
transactions only. `control` is derived and legitimately differs between a node
that rebuilt it and one that is still carrying nullopt after a sync.

usage: shadow_verify.py <work-dir>
"""
import hashlib
import json
import os
import sqlite3
import sys
from collections import defaultdict


def node_dirs(work):
    """Committee homes, in node order: server is node 0, clients follow."""
    boot = os.path.join(work, "bootstrap")
    homes = [("node-0", os.path.join(boot, "server", "data"))]
    for index in range(1, 7):
        homes.append((f"node-{index}", os.path.join(boot, f"client{index}", "data")))
    # Nodes the harness saw die (chaos kills) are behind by design; skip them.
    skipped = {f"node-{index}" for index in os.environ.get("EXC_VERIFY_SKIP", "").split()}
    return [(name, path) for name, path in homes if os.path.isdir(path) and name not in skipped]


def section_hashes(data_dir):
    """Live WAL databases must be read through the backup API, never opened
    directly — a direct read of a busy sqlite file gives torn or stale rows."""
    db = os.path.join(data_dir, "dag", "hot", "HotSections.db")
    if not os.path.exists(db):
        return None
    try:
        src = sqlite3.connect(f"file:{db}?mode=ro", uri=True, timeout=5)
        dst = sqlite3.connect(":memory:")
        src.backup(dst)
        src.close()
    except sqlite3.Error as error:
        print(f"  ! cannot read {db}: {error}")
        return None

    hashes = {}
    for section, payload in dst.execute("SELECT section, payload FROM sections"):
        try:
            body = json.loads(payload)["transactions"]
            hashes[section] = hashlib.sha256(
                json.dumps(body, sort_keys=True).encode()
            ).hexdigest()
        except (ValueError, KeyError, TypeError):
            hashes[section] = hashlib.sha256(payload if isinstance(payload, bytes)
                                             else str(payload).encode()).hexdigest()
    dst.close()
    return hashes


def intent_receipts(data_dir):
    """Finalized intent receipts per node — the shadow-specific half of the check."""
    found = {}
    for root, _dirs, files in os.walk(data_dir):
        for name in files:
            if "intent" not in name.lower() or not name.endswith((".db", ".sqlite")):
                continue
            path = os.path.join(root, name)
            try:
                src = sqlite3.connect(f"file:{path}?mode=ro", uri=True, timeout=5)
                dst = sqlite3.connect(":memory:")
                src.backup(dst)
                src.close()
                for table in ("consensus_intent_receipts", "consensus_intents"):
                    try:
                        count = dst.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
                        found[table] = found.get(table, 0) + count
                    except sqlite3.Error:
                        pass
                dst.close()
            except sqlite3.Error:
                pass
    return found


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    work = sys.argv[1]
    homes = node_dirs(work)
    if len(homes) < 2:
        print(f"FAIL: need at least two node homes under {work}")
        return 1

    print(f"=== cross-node content verification: {work} ===\n")
    per_node = {}
    for name, path in homes:
        hashes = section_hashes(path)
        if hashes is None:
            print(f"FAIL: {name} has no readable hot store")
            return 1
        if not hashes:
            print(f"FAIL: {name} has no hot sections to compare")
            return 1
        per_node[name] = hashes
        print(f"{name}: {len(hashes)} hot sections, range "
              f"{min(hashes) if hashes else '-'}..{max(hashes) if hashes else '-'}")

    common = set.intersection(*(set(h) for h in per_node.values()))
    union = set.union(*(set(h) for h in per_node.values()))
    print(f"\ncommon sections: {len(common)}, union: {len(union)}")

    # Only compare where every node has the section: a node that simply has not
    # received a range yet is behind, not diverged. Counting the whole union as
    # "missing" once produced a false 20000-section alarm.
    diverged = sorted(
        s for s in common
        if len({per_node[n][s] for n in per_node}) > 1
    )

    # Missing is only meaningful inside the range everybody has started, so take
    # the highest lower bound across nodes as the floor. Within that range, compare
    # against what some node actually holds: hot storage only keeps recent sections,
    # older ones live in packs, and a node that has not packed yet drags the floor
    # down to zero — every section below the others' hot window then looks "missing"
    # on all seven at once, which is nobody diverging from anybody.
    floor = max(min(h) for h in per_node.values() if h)
    ceiling = min(max(h) for h in per_node.values() if h)
    held_by_someone = {s for h in per_node.values() for s in h if floor <= s <= ceiling}
    missing = defaultdict(list)
    for name, hashes in per_node.items():
        for section in sorted(held_by_someone):
            if section not in hashes:
                missing[name].append(section)

    print("\n--- verdict ---")
    ok = True
    if diverged:
        ok = False
        print(f"DIVERGED: {len(diverged)} sections differ in transactions")
        for section in diverged[:10]:
            variants = {}
            for name in per_node:
                variants.setdefault(per_node[name][section], []).append(name)
            print(f"  section {section}: " +
                  " | ".join(f"{h[:8]}={','.join(n)}" for h, n in variants.items()))
        if len(diverged) > 10:
            print(f"  ... and {len(diverged) - 10} more")
    else:
        print(f"content: OK — all {len(common)} common sections identical")

    if missing:
        ok = False
        print(f"MISSING inside the common range {floor}..{ceiling}:")
        for name, sections in missing.items():
            print(f"  {name}: {len(sections)} sections, first {sections[:5]}")
    else:
        print(f"coverage: OK — no holes in {floor}..{ceiling}")

    tips = {name: max(h) for name, h in per_node.items() if h}
    spread = max(tips.values()) - min(tips.values())
    # The nodes are killed one after another, so whoever dies last commits another
    # checkpoint or two in the meantime — a spread of a few checkpoints is shutdown
    # skew, not divergence. A checkpoint covers a fixed span of sections, so read
    # that span from the tips the nodes actually stopped at.
    distinct = sorted(set(tips.values()))
    gaps = [b - a for a, b in zip(distinct, distinct[1:])]
    span = min(gaps) if gaps else 0
    checkpoints_behind = spread // span if span else 0
    print(f"height spread: {spread} sections"
          + (f" ≈ {checkpoints_behind} checkpoint(s) of {span}" if span else "")
          + f" (tips {min(tips.values())}..{max(tips.values())})")
    if checkpoints_behind > 3:
        ok = False
        print("  ! more than 3 checkpoints apart — nodes are not tracking the same tip")

    print("\n--- intents ---")
    for name, path in homes:
        receipts = intent_receipts(path)
        if receipts:
            print(f"{name}: " + ", ".join(f"{k}={v}" for k, v in sorted(receipts.items())))

    print("\nRESULT: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
