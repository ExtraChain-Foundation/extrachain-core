# TODO — DAG consensus & DFS reliability

Open items left after the 2026-08-07/08 hardening session. Everything listed as **done**
is already committed on this branch; the open items are what a stand run still shows or
what we deliberately postponed.

---

## Open

### 0. Vectors and dictionaries need concurrent load testing

A two-node stand now covers vector and dictionary creation, row replication, a node
restart, recovery of missing local files, and byte-for-byte convergence. Unit tests also
cover deterministic conflict ordering. The structured-data path has not yet been tested
with concurrent writers and repeated node restarts.

This matters more than the file path: vectors carry chat history, profiles and
dictionaries, they replicate row-by-row rather than as an immutable blob, and rows are
signed individually — a whole class of races the file path cannot expose.

What the stand needs: create vectors on several nodes, append rows continuously from
different nodes to the same vector, and audit that every node converges on the same row
set (not just the same dir row). Encrypted variants (`DataSecurity::Key` / `Self`) should
get their own pass, since decryption failures look like missing data.

### 1. Closed-section controls need a long stand run

Controls are now computed only for intervals that are below both the chain tip and the
cache watermark. A late transaction cannot change an interval after it is sealed. Unit
tests and a short two-node DAG and DFS stand pass. A long combined stand run is still
required. The control-hash metric must stay clean for the whole run (see `TESTING.md`).

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

- **Synchronous node shutdown** — the wrapper now stops DAG admission and network
  reconnects, then stops worker pools before it deletes the node. The Windows two-node
  runner exits with code `0` after it reaches the target section.
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
