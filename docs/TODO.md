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
