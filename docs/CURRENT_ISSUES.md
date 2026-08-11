# Current issues — state as of 2026-08-10

Snapshot before a large incoming patch. Everything here was measured on a six-node stand,
not inferred. The point of this file is that the findings survive the patch: whatever the
patch changes, these are the questions to re-ask afterwards.

**Status:** clean load is verified green; chaos (kills and freezes) still exposes four
defects, one of which is fixed and verified.

---

## What is verified working

Five-hour clean run, six nodes, same seed as the failing run before the fixes:

| | before | after |
|---|---|---|
| Verdict | FAIL 0/159 | **PASS 121/121** |
| Section gaps | one node missing 1-4 forever | **none** across 18319 sections |
| Vector rows lost (= chat messages) | **459** | **0** |
| Control divergence | 499 | **0** |
| Balances | 4 of 6 actors disagreed | identical, and match the chain |
| `database is locked` | 160 | **0** |

Under chaos (48-60 kills/freezes): DFS files never lost a single file (42/42), vectors
never lost a *file*, dictionaries never diverged.

**Read this as narrower than it looks.** All of it was measured on Full nodes, on chains
built from zero. Two things it therefore says nothing about: existing installations, which
carry damage the new code cannot see (§0.0 in TODO.md), and `DfsMode::Selective`, which
has never been run at all (§0.44) — it is the only mode with an incomplete catalogue, and
a complete catalogue is what every repair path here assumes.

---

## Open defects

### A. A node that falls behind never catches up — **FIXED, verified**

A node returning from kills/freezes came back 65 sections behind and the gap *grew*
(65 → 137 in two minutes, then stuck at 2256 while the network reached 2420). It rejected
every incoming transaction as `TooSectionDiff` — 606 rejections against ~130 on healthy
peers — and contributed nothing.

Cause was not a missing check but a disabled one:

```cpp
if (info.last_section_id > my_index) {
    // need_sync = true;      <- commented out
    need_recontrol = true;    <- goes to compare control hashes, then returns
}
```

The node *saw* it was behind and went to verify control hashes instead of fetching
sections. Fixed in `0afe5580`: behind by more than the acceptance window (15) now means
sync; less than that stays a control matter.

A/B on the same seed with chaos: `TooSectionDiff` **606 → 0 on all six nodes**, all nodes
ended level at 996.

### B. A rejected transaction is lost by the whole network

A node that was frozen keeps emitting transactions stamped with the section it last knew.
By the time it resumes, every peer rejects them as too old and **nobody retries**.

Measured: ~30 lost per node over 80 minutes of chaos. Four rejected hashes checked against
every section file on two nodes — present nowhere. Each hash appears exactly once in the
log, so no retry was attempted.

**Caveat on the evidence:** two nodes of six were checked. "No node in the network accepted
it" is consistent with the data but not proven by it — worth re-measuring across all six.

The feedback path already exists and is unfinished rather than absent: the sender learns
of the rejection, files the transaction in `failed_transactions_`, emits `dagTxNotApproved`
— and nothing ever reads either. The map is only inserted into and counted for a log line.

**Retrying is the wrong primary answer.** The real defect is upstream: a node emits
transactions without knowing the current section. Fix A removes most of this at the source,
because a node that syncs on resume stops producing stale stamps in the first place. A
bounded retry is worth having as a safety net for the remaining race, not as the mechanism.

### C. A restarted node writes into a section the network has closed

After a disruption a node accepts a transaction that is late by its own reckoning but
still inside its window, and writes it into a section everyone else has sealed.

Measured: exactly **one section out of 997** diverges in content (all other "mismatches"
in the harness metric are control fallout — see the note on metrics below).

```
d1,d3,d5,d6 sec49: 0beda1e3 27bef24e c0e503bc fb486bc7
d2,d4       sec49: same + fe11838f          (sec50 identical on all six)
```

Not fixable by fetching: the section is present with *extra* content, and section sync
merges — merging cannot remove. This is the concrete form of §1 in TODO.md, and the
stronger statement: section *membership* stays mutable after a disruption, not just the
control hash computed over it.

### D. Vector rows broadcast while a node is down are never resent

`DfsVectorAdd` is fire-and-forget. A node that is dead or frozen at that moment misses the
row, and nothing reconciles rows afterwards. Measured: 13 rows of 304 lost across 60
disruptions, evenly spread, ~1 row per 4-5 node failures. Permanent — sampling the same
ids 90 seconds apart showed zero caught up while the vector kept growing.

**This one genuinely needs the catch-up mechanism, and it is not a workaround.** The
contrast proves it: in the same run files lost nothing, because a file has the property
"I know it exists, therefore I can fetch it" — the `.dirs` row replicates, the node sees a
row with no payload, queues it, downloads it. Vector rows have no equivalent: they arrive
by broadcast or never. A vector can say "I exist"; it cannot say "I hold N rows, digest X".
That missing half of the protocol is the defect, not the absence of a retry.

---

## Recurring pattern worth checking after the patch

Four separate times in two days the mechanism was **written and disabled**:

| Where | How |
|---|---|
| `network_request_dir_rows` | unconditional `return;` at the top (`c44121ea`, 2025-05) |
| control-mismatch recovery | `return;` before the recovery branch, added in the same commit that implemented it (`9608d71b`, 2025-08) |
| `failed_transactions_` | filled, never read |
| behind-the-network sync | `// need_sync = true;` commented out |

When something does not happen, look for the disabled path before assuming it was never
built.

---

## Metric caveat

The harness sha256s the whole section file, which contains both the transactions and the
`control` field. One genuine content divergence therefore makes every later control
boundary hash differently, and the counter climbs (3 → 33) as if the split were spreading.
Compare transaction *sets* with the control stripped before drawing conclusions: in the
run above that turned 33 "mismatches" into exactly one.

---

## After the patch, re-ask

1. Do A/B against the chaos seed (`HARNESS_SEED=9110`, chaos on): `TooSectionDiff` should
   stay at 0 and no node should fall behind.
2. Does B still lose transactions? Check across **all six** nodes this time.
3. Does C still occur — one diverging section per ~1000?
4. Does D still lose rows, and does the patch give rows any reconciliation?
5. Re-run `audit_gaps.py`, `audit_balances.py` and the vector row audit; all three were
   zero on the clean run and are the fastest regression signal.
