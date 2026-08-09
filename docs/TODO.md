# TODO — DAG consensus & DFS reliability

Open items left after the 2026-08-07/08 hardening session. Everything listed as **done**
is already committed on this branch; the open items are what a stand run still shows or
what we deliberately postponed.

---

## Open

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

*Restart, 02:54:14 — the node comes up with an empty chain and does not sync at all:*
```
[Dag] Loaded: 0, first: 0, last cached: -1
[Dag] Started. Mode: DagMode::Full
… no start_check, no handle_sync_request, no section request, ever again
```
The whole log contains `Started` twice but the sync sequence only once. After the restart
the node went straight to accepting live traffic, whose first transaction was in **section
5** — so 1-4 were skipped without a trace.

Why the existing genesis guard did not catch it: `start_check` returns early when section 0
is present and non-empty, and it *was* present (fetched on the first start). "Has genesis"
was used as a proxy for "chain is intact", and those are different claims. A node holding
section 0 and nothing else looks healthy to that check.

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
