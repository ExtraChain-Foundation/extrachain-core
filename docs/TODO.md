# TODO — DAG consensus & DFS reliability

Open items left after the 2026-08-07/08 hardening session. Everything listed as **done**
is already committed on this branch; the open items are what a stand run still shows or
what we deliberately postponed.

---

## Open

### 0.0 Existing installations carry damage the new code assumes away

**Not urgent, but required before any of this reaches real users.** Everything fixed on
2026-08-09 was verified on chains built **from scratch** in a stand. A node that has been
running with the old code has state that the new logic quietly assumes cannot exist:

| Damage already on disk | Detected by the new code? | Repaired? |
|---|---|---|
| Missing sections in the middle | yes — contiguity scan | yes |
| Wrong control on a **past** boundary | **no** — only new boundaries are compared (§0.475) | no |
| Vector rows dropped by `database is locked` | no — nothing knows they existed (§0.45) | no |
| Dir rows lost the same way | no (§0.4) | no |
| Balances derived from an incomplete chain | no — the cache faithfully sums what it has (§1.1) | only after the chain is repaired |

The sections case is the lucky one: it self-heals now. The rest do not, because the
node has no record that anything is missing — a lost vector row leaves no trace, and an
old control is never re-read.

Concretely, before shipping:

1. **Decide the policy per damage class** — repair in place, refetch from peers, or
   rebuild from genesis. These are very different costs and only the first two are viable
   on a phone.
2. **A one-time verification pass on upgrade**: re-verify all control boundaries (not just
   the newest), and reconcile vector row sets against peers. Expensive, but it runs once.
3. **Decide what a node does when it cannot repair itself.** Right now it would keep
   serving wrong data with full confidence — `range` reports a complete chain, the balance
   cache reports a balance. Failing loudly beats being quietly wrong about money.

Until this exists, the fixes stop the bleeding for **new** chains and do nothing for the
damage already accumulated.

### 0. Vectors and dictionaries are not covered by load testing

Stand runs so far exercised plain files only: a typical run replicated ~235 files
(`FileType::File`) against 3 vectors and 1 dictionary — and those four were created by
the core itself at startup, not by the stand. So the whole structured-data path
(`store_vector`, row appends, `DfsVectorContent` replication, per-row signatures,
`DataSecurity` variants) has never been under concurrent load with node restarts.

This matters more than the file path: vectors carry chat history, profiles and
dictionaries, they replicate row-by-row rather than as an immutable blob, and rows are
signed individually — a whole class of races the file path cannot expose.

What the stand needs: create vectors on several nodes, append rows continuously from
different nodes to the same vector, and audit that every node converges on the same row
set (not just the same dir row). Encrypted variants (`DataSecurity::Key` / `Self`) should
get their own pass, since decryption failures look like missing data.

### 0.4 Why is the dir row missing in the first place? (dirs distribution strategy)

While fixing vector replication we made the download queue accept a vector that has no
local dir row (it previously required one, so a vector first seen through a sync was
never queued). That is a correct guard, but it treats a symptom: the real question is
**why a node lacks the dir row at all**, and what the intended distribution model for
`.dirs` is.

Right now dirs arrive opportunistically — partly through the creation broadcast, partly
through handshake sync — and a node that was busy at the wrong moment can stay without a
row indefinitely, or hold a row whose payload never follows. There is no explicit policy
saying who is supposed to know what.

**Partly answered 2026-08-09: some dir rows are simply lost on write.** The same
`database is locked` failure that was dropping vector rows (§0.45) also hits the dirs
catalogue — `INSERT OR REPLACE INTO ActorsFiles` failed twice on the night run, on the
node that ended up worst off. Rare next to the 123 vector-row failures, but the same
class: the row is dropped, nothing retries, and the node never learns the file exists.
So "why is the row missing" has at least one concrete mechanical answer, now fixed by the
busy timeout in `c1a05508`. The distribution-model question below still stands — a busy
timeout reduces the loss rate, it does not give the node a way to notice a row it never
received.

**Chosen model: light by default, high-light as a priority overtake on top of it.**

- **light (the default, always).** A node pulls the *whole* `.dirs` catalogue — metadata
  only, not payloads — and keeps it complete. This is the base: without the full
  catalogue a node cannot know what it is missing, so no repair, catch-up or audit has
  anything to compare against. A payload is then fetched lazily, but the fact of its
  existence is never in doubt. This is also what makes the stand honest: the vector audit
  measures convergence against what a node *knows* it should hold.
- **high-light (urgent).** Not a separate mode and not a replacement — an overtake. When
  something is needed *now* (a chat is opened), fetch that specific row plus its content
  immediately, ahead of the background catalogue work. The background pull of the full
  `.dirs` continues regardless and is never cancelled by it: fetch what is needed first,
  then still request the full catalogue.

The order matters. The inverse — fetch only what is asked for, and the full catalogue
"when needed" — reproduces exactly the state we are trying to leave: a node that never
learns a vector exists, for which no catch-up can help because it has nothing to
reconcile against.

The full design, including what already exists in the code and what has to be built, is
in `docs/DIRS_SYNC.md`. Key finding from writing it: `network_request_dir_rows` — the
handler for a *targeted* dir-row request — is disabled by an unconditional `return;`, and
no path fetches rows incrementally (`get_dir_rows(db_, actor, 0)` always reads from zero).
So a node literally has no way to ask for what it is missing: the only live option is a
full dump. That is the mechanism behind this whole item.

Mechanism is the second question, once the model is fixed: how `.dirs` is obtained
efficiently (incremental deltas, digest comparison with peers, periodic reconciliation)
so that both "row known, payload missing" and "row unknown" self-heal. The current queue
guard stays as a safety net either way, not as the answer.

### 0.45 Vector rows are lost in transit and never re-requested

> Structural fix designed in `docs/DB_POOL.md`: a bounded sqlite connection pool with LRU
> eviction and per-file exclusivity. A long-lived connection *per file* is not viable —
> the stand already reached 613 vector files per node, and Android/iOS cap file
> descriptors around 1024. The busy timeout and the off-thread write stay as the layer
> below it, not as duplicates of it.

**Found 2026-08-09 on the night run. This is chat message loss.** Vectors carry chat
history, so a row that never arrives is a message the user will not see.

Measured on the six-node run, auditing every vector that actually holds data (the stand
creates hundreds of vectors but only a handful are written to — the rest are empty and
prove nothing):

| Vector | Union of rows | Rows missing per node |
|---|---|---|
| `5b7d5346` | 265 | d1:7 d2:13 d3:8 d4:0 d5:4 d6:5 |
| `5ff6b708` | 273 | d1:0 d2:0 d3:1 d4:4 d5:11 d6:8 |
| `3ef29d38` | 253 | d1:0 d2:0 d3:0 d4:0 d5:1 d6:1 |

Three of eight vectors with data are incomplete somewhere, and **every node loses
something** — 4 to 16 rows each. It is not one bad node: the node holding the complete
set differs per vector.

What rules out "still in flight":

- sampling the same vector 40 s and 75 s apart returns **the identical missing id list** —
  not one row caught up in either interval, while new rows kept arriving normally;
- `ONLY_ON_THIS_NODE = 0` everywhere: no node holds a row the others lack, so this is
  pure loss, never divergence. Laggards are strict subsets of the union.

That combination is the signature: replication delivers new rows fine, so the gap stays
at a roughly constant size and *looks* like lag, but the rows already dropped are gone for
good. Nothing detects or re-requests them — the same shape as the DAG section gap in §1.1,
one level up.

Note this is invisible to the existing `full_copies` metric, which only asks whether the
vector *file* exists on each node. It was 200/200 for the whole run while rows were being
lost. Row-level comparison against the union is what surfaces it.

**Final numbers, audited after the run ended and the nodes were stopped** — i.e. with no
load, nothing in flight, and every chance to settle:

```
vectors with data on all 6 nodes : 24
  still incomplete               : 4
rows lost per node               : d1:74 d2:55 d3:79 d4:18 d5:139 d6:94
TOTAL rows lost                  : 459

  5ff6b708: union=943  missing={d1:27 d2:0  d3:15 d4:18 d5:15 d6:11}
  8e287b0f: union=941  missing={d1:3  d2:4  d3:21 d4:0  d5:89 d6:66}
  3ef29d38: union=906  missing={d1:37 d2:38 d3:33 d4:0  d5:30 d6:12}
  5b7d5346: union=958  missing={d1:7  d2:13 d3:10 d4:0  d5:5  d6:5}
```

459 rows gone for good over a 6.5 h run, every node affected, worst case 89 of 941 rows
(~9% of one chat's history) missing on a single node. This settles the question of whether
it was lag: it was not.

**The losses come in bursts, not evenly.** Ordering the complete row set by timestamp and
marking each node's gaps by position in that sequence:

```
d1: missing 16 at positions 302, 320, 324, 330-331, 337-338, 342-343, 347-351, 360, 364
d5: missing 15 at positions 238, 242-243, 247, 253, 258, 262-263, 266, 270-271, 274, ...
```

Both are clustered — d1 loses everything in the last quarter of the chain, d5 in a band
around positions 238-281 — rather than dropping every n-th row. So a node stops accepting
for a stretch and then recovers, which points at a saturated or briefly dead socket rather
than a per-row validation failure.

Supporting evidence from the logs: the two nodes that *received* the most
`Vector content package` messages (2424 and 2837) are also among the biggest losers, so
packets are arriving — something after receipt drops them.

**Cause found: the sqlite write fails with `database is locked` and nobody retries.**

The node log at the exact second of a lost row:

```
05:05:46.846 [Warning] [DbConnector] ImplementationInsert: Execution failed: database is locked
  …/5ff6b708…: INSERT OR REPLACE INTO Vector ('id','message','sign','actor','timestamp','status')
```

Matching failure counts against measured losses across the run:

| Node | `INSERT INTO Vector` failures | Rows actually missing |
|---|---|---|
| d1 | 31 | 24 |
| d2 | 14 | 0 (holds the complete set) |
| d3 | 23 | 12 |
| d4 | 22 | 16 |
| d5 | 34 | 15 |
| d6 | 28 | 11 |

The node that hit the lock least is the only one with a complete vector. So the loss is
not in the network at all — the packet arrives and is then dropped by a contended sqlite
file. That also explains the bursts: lock contention comes in waves under load.

The row disappears silently because nothing on the path treats a failed write as an
event worth reacting to:
- `DbConnector` logs a warning and returns false — no retry, no busy-timeout backoff
  (`db_connector.cpp:673`);
- `DfsVector::local_add` propagates that false, but also returns `true` when it *skips* a
  row as stale (`dfs_vector.cpp:461`), so "written", "skipped" and "failed" collapse into
  one boolean;
- `DfsController::network_vector_add` ignores the result entirely except for the UI signal
  (`dfs_controller.cpp:1847`).

Fix direction, cheapest first: give the sqlite connection a busy timeout so a contended
write waits instead of failing outright; then make a genuinely failed row write
re-queue rather than vanish; and separate "skipped as stale" from "write failed" in the
return type, because a silent boolean is what let this run for hours unnoticed.

**Is 5 s enough?** Measured on the run: 172 lock failures across four nodes fall into 149
episodes (failures within 5 s of each other grouped together). Of those, exactly **one**
spanned more than 5 s — and that one was three separate attempts over 7 s, not a single
long-held lock. The largest episode is 3 failures. So contention here is genuinely
short-lived and `sqlite3_busy_timeout(5000)` in `c1a05508` covers effectively all of it.

That is an argument for the timeout being the right first fix, not for it being the whole
fix: "effectively all" is not "all", and the second layer — noticing and re-queueing a
write that fails anyway — is what turns a reduced loss rate into no loss at all.

### 0.47 The node detects a control mismatch and does nothing about it

Control hashes are the one mechanism that reliably catches missing history — they folded
the whole 1..20 interval into a single value and exposed the gap that every direct
section-by-section comparison called healthy (§1.1). Detection works.

Acting on it does not. `network_response_hash_interval` compares the peer's control with
its own and, on a mismatch, logs `Need sync` and returns:

```cpp
if (last_control->control != hash_interval.hash) {
    eLog("[Dag] Hash interval check: false. … Need sync", …);

    // this->start_sync();
    return;                                   // <- everything below is dead code
    if (current_section_ < hash_interval.to) {
        …start_check();                       // we are behind: sync
    } else {
        …request_file_sections(from, to);     // we differ in a known range: refetch it
    }
}
```

The recovery logic is fully written and unreachable. `git blame`: the `return` was
introduced **in the same commit that implemented the feature** — `9608d71b [Core] Dag:
implement control-based sync` (#156, 2025-08-29) — so it has never run.

This matters directly for §1.1: the node that lost sections 1-4 saw its control for
section 20 disagree with the rest of the network every time it was asked, for 6.5 hours,
and never acted on it. Re-enabling this would have repaired the gap without any of the
new gap-detection code.

Before switching it on, work out why it was disabled at birth — the obvious hazard is a
sync storm when many nodes disagree at once, or a loop when the mismatch cannot be
resolved (the same shape as the retry budget in `1ba9fa7e`).

**Measured 2026-08-09: controls alone are not enough to repair a hole.** Tempting theory —
skip the local contiguity scan entirely and rely on control comparison, since
`hash_interval` already hashes a missing section as empty (`dag.cpp:3126`), so an empty
section and a lost one produce *different* interval hashes by construction. A/B within one
build, same test (delete sections 15-18 from a node, restart, settle):

| | outcome |
|---|---|
| gap scan on | `REPAIRED`, log shows `chain is missing section` + `gap at section` |
| gap scan off | `NOT REPAIRED`, log shows **nothing** — no control comparison ran at all |

The reason: the control check in `send_sync_request` (`last_control->control !=
info.last_control_hash`) only runs *inside* a sync that has already started, and without
the scan nothing starts one — `start_check` returns early. Controls detect divergence once
you are talking; they do not initiate the conversation.

So the two mechanisms are complementary, not alternatives: the scan starts the sync, the
control decides what is actually wrong. Controls would still be the only way to catch a
section that is present but *corrupt*, which the scan cannot see at all.

### 0.475 A control that is already wrong on an old boundary is never re-checked

Interval verification only fires when the cache advances onto a **new** boundary
(`DagCache::check_and_update_cache` → `DagIntervalHash`). Nothing ever revisits a boundary
the chain has moved past, so a control that was already wrong when we started looking
stays wrong forever.

Measured 2026-08-09: corrupt the control at boundary 20 on one node of a two-node network
(leaving the chain itself intact, so only the control path could react), then let the chain
grow to 89 and settle.

```
h1 sec20 control: 7b6c059b652b5ffe     correct
h2 sec20 control: ffffffffffffffff     still corrupt
log:              no "Control mismatch" at all — never even compared
```

`write_control` *would* overwrite it — a differing hash falls through the
`section->control == hash` early return and is written. The gap is purely that nothing
asks about old boundaries.

**Not a problem for a chain built from scratch**: in the same run every control boundary
(0, 20, 40, 60) matched on both nodes, including after a gap at 15-18 was detected and
repaired — the control was recomputed correctly. This only bites data that was *already*
damaged: a node with a corrupted store, or the residue of an earlier bug.

Options when it becomes relevant: a slow background reconciliation of old boundaries, or
checking a few random past boundaries on each handshake (faster detection, slightly more
traffic). Neither is urgent while nodes start clean.

### 0.46 An actor folder is created for every known actor, not for actors we store

`Dfs::initialize_actor_folder` is called on every saved actor
(`dfs_controller.cpp`, `ActorIndex::actorSaved`) and for every owner in a dirs sync
(`dirs_manager.cpp`). A node knows hundreds of actors and stores content for a handful,
so the disk fills with empty directories and "does this actor have data" stops being
answerable by looking at the filesystem.

The right rule: **a folder appears when content lands, and never for an empty actor.**

Removing the two eager calls is not enough on its own — tried 2026-08-09 and it broke
replication (`full_copies` 200/200 → 0/200, owner dirs 18 → 4), reverted in `97a6e25b`.
The creation paths do not all make their own directory:

| Path | Creates its own dir? |
|---|---|
| file download (`load_manager.cpp:567`) | yes |
| incoming vector content (`handle_package`) | yes, since `5ff722a6` |
| adding a local file (`dfs_controller.cpp:206`) | yes |
| **`DfsVector::create`** — writes `.vector` and opens the db | **no** — relied on the eager call |

So the order is: make every creation path responsible for its own directory (starting
with `DfsVector::create`), verify replication is unaffected, and only then drop the eager
calls. Doing it the other way round removes the safety net before the replacement exists.

**Done 2026-08-09** for vectors (`f44fba1a`, `ecdb97ce`): `DfsVector::create` now owns its
directory, both eager calls are gone, and `handle_package` validates the package *before*
creating anything — a rejected or unsolicited answer leaves no empty folder behind.
Measured: owner directories per node 18 → 5, empty ones 0, replication unchanged at
`full_copies=200/200`.

**Still eager for files.** `LoadManager` creates the owner directory when a file is
*queued* (`load_manager.cpp:567`), not when the first fragment lands. A download that is
queued and then never completes — peer gone, file withdrawn, node restarted — leaves an
empty directory. Moving it to the first fragment write is the same fix, but the file path
writes fragments from several places, so it needs its own pass and its own verification.

### 0.48 A console command during startup crashes the node (SIGSEGV)

Reproduced 2026-08-09 while testing the gap fix. A node was restarted and a
`connect ws <ip>` line was written to its stdin before initialisation finished:

```
11:43:48.649 [Console] Input: connect ws 127.0.0.2
11:43:48.649 [Critical] Catch signal: Segmentation fault: 11
```

The crash lands before `[Dag] Started` and before `Start listening`, i.e. the command is
processed while the node is still constructing its managers. Starting the same node on the
same data *without* sending a command comes up fine, so it is the early command, not the
state on disk.

Two things worth doing:
- refuse (or queue) console input until the node reports it is started, instead of
  dereferencing half-built state;
- the signal handler prints only the signal name — no backtrace, so a crash in the field
  tells us nothing. Worth printing a stack.

Found via a harness bug that made this easy to hit: `wait_log` matched `Start listening`
from the *previous* life of the process, so the harness thought the node was ready
immediately. That is fixed in the stand, but the core should not crash regardless.

### 0.5 A node can report "listening" while its listener accepts nothing

Seen once on a six-node stand (not reproducible on a rerun of the same seed, so a startup
race): the last node to start logged `Start listening: <ip>:<port>`, but from that moment
its network layer went completely silent — zero incoming connections were ever accepted
(peers dialled it six times each), its single outgoing socket hung at `New service`
without activating, and no further network log line appeared. The process itself stayed
healthy: console commands worked, vectors were created locally, transactions were emitted
with `section: 1` for the whole run.

Two things worth fixing regardless of the root cause:
- the node has no self-check that would notice "I am listening but nobody ever connects
  and my only socket never activated" — it stayed in that state indefinitely;
- a stand cannot detect it either, because liveness looks fine from the outside. Any
  harness should treat "zero peers for N minutes" as a failure, not as idleness.

### 0.7 Transaction timestamp is attacker-controlled and unvalidated

`prove_transaction` never checks `timestamp` — not against the local clock, not against
the previous section, not at all. The field is signed, but a signature only proves
authorship, not truthfulness: the sender picks the value.

It is already load-bearing in two places:
- **Anti-spam.** `network_transaction` stores `last_txs_[sender] = transaction.timestamp()`
  and later compares it against the local clock (`current_date_ms() - stored < 4500`).
  A sender who backdates the field makes the difference arbitrarily large, so the rate
  limit never triggers — the limit is bypassed by one unvalidated number.
- **Section content hash.** `Transaction::operator<` orders by `timestamp` before `hash`,
  and `Section::calculate_hash` concatenates transactions in set order — so the field
  influences the consensus hash of a section.

This also rules out using transaction time as a "closing" boundary for control hashes:
anything derived from it is attacker-steerable. Section-based watermarks (derived from
chain height, not from any clock) do not have this problem.

Minimum fix: reject a transaction whose timestamp is further ahead of local time than a
small tolerance, and further behind than the acceptance window allows; keep anti-spam
state on the receiver's own clock rather than on a value the sender supplies.

### 0.9 A restarted node accepts a transaction into a section the network has closed

**Found 2026-08-10 by the first chaos run after all the fixes.** This is the defect the
clean runs could not show: with `NO_CHAOS` the divergence never appeared, and with kills
and freezes it appeared within seven minutes and kept growing (mismatches 0 → 0 → 6 → 9).

Section 49 on a six-node network:

```
d1,d3,d5,d6 sec49: 0beda1e3 27bef24e c0e503bc fb486bc7
d2,d4       sec49: 0beda1e3 27bef24e c0e503bc fb486bc7 + fe11838f
both groups sec50: c6c4a851            (identical — only 49 differs)
```

`fe11838f` is not missing anywhere; it is **extra** in the two nodes that had just been
disrupted — d2 was killed at 01:25:35 and stored it at 01:25:47, d4 had been frozen at
01:23:26. Everyone else had already moved past section 49 and placed the same transaction
in a later section.

So a node that was dead or frozen comes back, receives a transaction that is late by its
own reckoning but still inside its acceptance window, and writes it into a section the
rest of the network has already sealed. The section set — not just the control hash —
stays mutable behind the network's back.

Notes that matter for the fix:

- **The gap-repair logic added on 2026-08-09 does not help here.** It looks for *missing*
  sections; here the section is present with extra content. Different failure, different
  detector.
- Control hashes do detect it (that is how it surfaced), but the recovery path refetches
  the interval — and a refetch merges, so an extra transaction is not removed by it.
  Removing content is a different operation from adding it.
- This is the concrete, reproducible form of §1 below. §1 says controls are computed over
  mutable sections; this shows the *membership itself* is mutable after a disruption,
  which is the stronger statement.

Reproduce: `HARNESS_SEED=9110 REAL=1 DAG_NODES=6` with chaos enabled (no `NO_CHAOS`),
~7 minutes.

### 1. Controls are computed over still-mutable sections → control chain splits

**The only remaining cause of divergence after all the fixes below.** Final 3.5h run:
46584 sections, transaction sets identical on all six nodes, but 459 control sections
differ, splitting the network into two stable camps.

Mechanism: the control hash for a `%20` section is written the moment the boundary is
crossed — i.e. *inside* the acceptance window. A transaction that is late but still
within the window invalidates `control` of its own section, and nothing re-chains the
controls after it (`start_control` walks forward from the *last* control and skips the
hole in the middle). From then on the two camps hash different bases forever.

Agreed direction (discussed 2026-08-08): **compute controls only over closed sections**,
i.e. below the cache watermark (`last_cached`). Control positions stay at `%20`, the cache
and the acceptance window are untouched — only the *moment* of computation moves. Then a
late transaction physically cannot hit an already-hashed section, and invalidation plus
re-chaining stop being needed at all. The cache is safe by construction: nothing is ever
inserted below the watermark.

Cost: a control appears with a delay of T (it is a seal on history — no hurry).
Verification: a combined DAG + DFS run — the control-hash metric must stay clean for the
whole run (see `TESTING.md`).

**Isolated measurement (2026-08-09, six nodes, real rate).** Comparing every section file
byte-for-byte between a diverged node and the majority, with the `"control"` field
stripped out, separates the two questions cleanly:

- sections compared: 276
- sections whose **transaction payload** differs: **0**
- sections where **only `control`** differs: 13 — and they are exactly 20, 40, 60, 80,
  100, 120, 140, 160, 180, 200, … i.e. every control section, without exception

That comparison covered only sections **present on both nodes**, which is exactly how it
misled: the diverged node was missing four section files outright, and a comparison of
what both hold cannot see a hole in one of them. See §1.1 — the control was right and the
data was not.

**Correction (2026-08-09, traced to the end): in the run below the control split was not
the defect — it was the detector.** See §1.1. What follows is the trigger of that
particular run; the mutable-section mechanism above remains a separate, real issue.

```
02:52:10.852  [Dag] sync_last_index: 0x0 / 0 sections
02:52:10.857  [Dag] File sync completed              <- 5 ms later, chain still empty
02:52:15.480  [Dag] Current: 0 (0x0) section, status: DagStatus::Ready
```

`network_response_file_sync` treats the sync as finished via `file_sync->to >=
sync_last_index_ - 1`, which on an empty chain is `0 >= -1` — true immediately. It then
calls `start_control()` (note: **no** `Force::Active`, so the early return in
`start_control` applies) over a chain of zero sections and lets the node reach
`DagStatus::Ready` while holding nothing.

From that point the guard `if (this->status_ != DagStatus::Ready)` around both
`start_control()` calls can never fire again: the node logged `Check controls...` exactly
once, at 02:52:10, and never again for the rest of the run. Sections then arrived through
the normal path and controls were written at each `%20` boundary on a base nobody ever
re-verified.

So there are two distinct defects here:
1. a node declares `Ready` and seals controls on an empty chain, because "sync completed"
   is derived from an index that is not yet known;
2. once `Ready`, nothing ever re-chains controls, so the initial split is permanent.

Fixing (1) removes this run's trigger; fixing (2) is what makes the control chain
self-healing in general, and is the "compute only below the watermark" direction above.

### 1.1 A joining node silently skips sections and reports a complete chain

**This is the actual defect found on 2026-08-09, and it is a data-loss bug, not a
hashing one.** The control mismatch above was the symptom that exposed it.

On a six-node run the last node to join permanently lacks sections **1, 2, 3 and 4** —
the files simply do not exist on disk — while holding section 0 and everything from 5
onward. Four transactions are gone on that node and never come back.

Two things make this worse than a plain gap:

- **The node believes its chain is complete.** Its `range` reads
  `{"first":"0","last":"2da","last_cached":"2bc"}` — identical to a healthy node's. There
  is no record that 1-4 were never received, so nothing will ever go looking for them.
- **It is invisible to a section-by-section comparison.** Diffing the sections two nodes
  both have shows perfect agreement, because a missing file is not a differing file. The
  gap only surfaced through the control hash, which folds *the whole interval* into one
  value: `control(20) = hash(control(0) + hash_interval(1..20))`. Section 0 matched
  byte-for-byte on both nodes, so the split had to come from the interval — and it did.

So the control chain did its job exactly as designed: it detected missing history that
every direct comparison called healthy. That is worth keeping in mind before changing
when controls are computed (§1) — the mechanism is load-bearing.

**Origin, proven from the log (not inferred).** Two separate failures compound:

*First start, 02:52:10 — the node asks for exactly one section and calls it a day:*
```
[Dag] handle_sync_request: genesis section missing — syncing from 0
[Dag] sync_last_index: 0x0 / 0 sections
[Dag] Request file sections from 0 to 0      <- asks for section 0 only
[Dag] File sync completed                     <- 5 ms later
```
`sync_last_index_` is the peers' height at handshake time (`nodes_by_block.front()`), which
was genuinely 0 — the network had just started. The request range is
`min(sync_last_index_, current + BATCH)` = 0, so only section 0 was ever requested, and
`file_sync->to >= sync_last_index_ - 1` (`0 >= -1`) immediately declared success. Sections
1-4 were produced by the network *after* this moment and were never asked for.

*Restart, 02:54:14 — the node comes up holding only section 0 and never syncs again:*
```
[Dag] Loaded: 0, first: 0, last cached: -1     <- current_section_ = 0: just the genesis
[Dag] Started. Mode: DagMode::Full
[WS] Start listening: 127.0.0.7:17593
[WS] Start sync...                              <- x5, one per peer connection
[ActorIndex] Diff size: 0, need: 18, local: 18  <- actor sync fine, so start_check() ran
… and yet: no handle_sync_request, no section request, ever again
```
This is the decisive part. The connection handler **did** reach `start_check()` — it fired
once per peer — but `start_check` returned immediately, because section 0 was present and
that is the only thing it checks. A node holding exactly one section, with five live peers
and a network 1900 sections ahead, decided it had nothing to do. It then accepted live
traffic starting at **section 5**, so 1-4 were skipped without a trace.

Why the existing genesis guard did not catch it: `start_check` returns early when section 0
is present and non-empty, and it *was* present (fetched on the first start). "Has genesis"
was used as a proxy for "chain is intact", and those are different claims. A node holding
section 0 and nothing else looks healthy to that check.

**The gap becomes a money discrepancy.** Checking `dag/cache/BalanceCache.db` across the
six nodes on the same run: four of six actor balances differ, and the gapped node is short
by exactly the rewards that were in the sections it never received.

| Actor | Reward in the missing sections 1-4 | Shortfall in the gapped node's cache |
|---|---|---|
| `08180ef02c48` | +1 (section 2) | −1 |
| `6637ac690740` | +2 (section 1) | −2 |
| `824f8e7a4527` | +2 (section 4) | −2 |
| `c7513ba48e57` | +1 (section 3) | −1 |
| **total** | **6** | **−6** |

Coin for coin. So the balance cache itself is not defective — it faithfully sums what the
node holds — but it inherits the loss silently and turns it into disagreement about
balances. Worth stating plainly because it raises the severity: a missing section is not
only a hole in history, it is a wrong balance that no consistency check currently
compares. `audit_balances.py` in the stand now does compare it.

What to fix, in order:
1. **Never conclude a sync from an index that is not yet known.** "Peers report height 0"
   must mean "nothing to compare against yet", not "we are up to date". Equally, a node
   that restarts with an empty (or short) chain while the network is far ahead must sync
   again — the current `start_check` guard tests only for the presence of section 0, so
   "I have genesis" is treated as "I have everything".
2. **A node must be able to notice its own gaps.** `range` records only first/last, so a
   hole in the middle is unrepresentable. Either track the received set, or verify
   contiguity when crossing a control boundary.
3. **A detected gap must be repairable** — which is §2 (backfill), now with a concrete
   case to test against, not a hypothetical one.

Reproduction: six nodes, real rate, seed 8090 (`HARNESS_SEED=8090 REAL=1 DAG_NODES=6`),
join the last node while the network is still near height 0. Check with:
`for i in 1 2 3 4; do ls dagdfs/d6/dag/0/$(printf %x $i); done`.

### 2. Backfill of missed sections inside the acceptance window

A node that was dead or frozen does not pull the transactions it missed
(`request_sections` is only reachable from a commented-out block in
`network_transaction`, `dag.cpp:311-324`). With the connection and integrity fixes in
place this is no longer a workaround but a genuine second line of defence.

Scope it to the **open window only** (`±15` sections): trigger on a detected gap between
`current_section_` and an incoming `tx.section()`, or a periodic control/section-hash
comparison with neighbours. Not a global catch-up.

### 3. DAG CPU: per-transaction cost grows with section size

Profile (`sample`, six-node stand): `Dag::read_section` parses the whole section JSON
(with BigNumber fields) for **every** incoming transaction, and the section is
re-serialized on every insert. Harmless at the real network rate (1–3 tx per section),
but it is what puts the ceiling at roughly 50 tx/s, where sections grow to thousands of
transactions.

Quick wins: keep the current section parsed in memory (invalidate on write), batch the
serialization instead of doing it per insert.

### 4. `TooOften` rate limit tuning

`network_transaction` drops a transaction if the same sender sent one less than 4.5s ago
(`dag.cpp:278`). Correct as anti-spam, but it also caps a single sender at ~0.2 tx/s —
worth revisiting before any load growth. Stands bypass it with a test-only env switch;
the limit itself is untouched in production code.

---

## Done in this session (for context)

- **Duplicate-connection arbitration** (`eb7117fe`) — a side-change in #152 commented out
  the `is_active` guard, turning dedup into newest-wins; mutual dials then fought forever
  and broadcasts sent into an about-to-die socket were silently lost.
- **Section write race** (`81766a68`) — read-modify-write without holding a lock; two
  concurrent transactions for the same section both read the old set and one insert was
  lost. `save_mutex_` now covers all six section writers.
- **Message id entropy** (`60abe242`) — seconds + `bounded(100000)` gave only 100k
  distinct ids per second network-wide; a collision meant the second message was silently
  dropped by receive dedup. Now `hash(body + msecs + random64)`.
- **Genesis section on join** (`ccdf6c56`) — a full node starts already `Ready`, so
  `start_check` never ran the initial sync and section 0 (the sync base, and the source of
  the network id) never arrived; `write_control` later materialized an empty stub and the
  control chain diverged permanently.
- **Transaction processing cost** (`daaa54aa`) — the copy/move constructors recomputed the
  blake hash on every copy, and the duplicate check walked up to 100 sections per incoming
  transaction. A 12-minute processing backlog became sub-second (108 → 1900+ sections/hour
  on the stand).
- **Dispatch path unclogged** (`69afef76`) — MB-sized fragment messages were deserialized
  and written to disk inline on the dispatch thread while the uploader blasted a whole
  window into the socket buffer; consensus messages queued behind megabytes and
  transactions fell out of the acceptance window (`TooSectionDiff`).
- **File integrity** (`d92c95bd`) — three holes producing full-size corrupted copies and
  stuck partials: `share_stored_file` served zeroes out of the unwritten holes of a file
  the node was still downloading; `write_file_chunk` fell back to a truncating open on an
  existing file after a transient `r+` failure; a fragment whose disk write failed was
  never re-requested.
