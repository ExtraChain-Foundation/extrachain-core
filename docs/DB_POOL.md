# SQLite connection pool — design

Why the current "open a connection per call" pattern loses data, why a long-lived
connection per file is not the answer either, and what to build instead.

Written after the 2026-08-09 night run lost **459 vector rows** — chat messages — to
`database is locked` (see `TODO.md` §0.45).

---

## 1. What is wrong today

Every call constructs its own `DbConnector`, i.e. its own sqlite connection to the same
file (`dfs_vector.cpp:214, 248, 399, 466, 566`). There *is* a global
`static std::recursive_mutex dbmutex`, and `implementation_insert` does take it — so the
natural question is why a lock error is possible at all.

Three reasons, and they compound:

1. **The mutex guards a call, not the file.** It is taken inside `select` / `insert` and
   released when that method returns. The *connection* outlives the method (it dies with
   the enclosing scope), and sqlite holds a read lock on the file for as long as the
   connection is in that state. So another thread can find the mutex free, take it, and
   still get `SQLITE_BUSY` from sqlite.
2. **Read and write use different connections.** `DfsVector::local_add` calls `read_row`
   (one connection) and then opens a second one to `replace`. A reader can block its own
   writer.
3. **Several writers are not guarded at all** — `insert`, `replace`, `update`,
   `create_table`, `hash_size` have no lock; only the low-level `implementation_insert`
   does, so some paths bypass the mutex entirely.

Result: the write fails, `DbConnector` logs a warning and returns false, nothing retries,
and the row is gone. Silently.

## 2. Why not one long-lived connection per file

The obvious fix — keep a connection open per database file — does not survive contact
with production. Vectors are per-chat, per-list, per-username: the stand already reached
**613 vector files per node** in one night, and real usage means thousands.

On desktop the file-descriptor limit is generous (1M on this macOS box), but on **Android
and iOS it is typically 1024**. A thousand open databases would exhaust it outright, and
the failure mode — "cannot open any file at all" — is far worse than the one being fixed.

## 3. The design: a bounded pool with eviction

Keep a small number of connections open, keyed by file path, and evict the
least-recently-used when the cap is reached.

```
handle = pool.acquire(path)     // reuses an open connection, or opens one (evicting LRU)
… use it …                      // handle is RAII: returns the connection on scope exit
```

Properties it must have:

- **Bounded.** A hard cap (say 32-64 connections) with LRU eviction. Memory and fd usage
  become independent of how many vectors exist.
- **Exclusive while held.** `acquire` on a file already checked out **waits** for the
  holder to finish. This is the part that actually fixes the bug: read-then-write in
  `local_add` becomes one uninterrupted span, so a reader can no longer block its own
  writer. Per-file waiting, not one global mutex — two different vectors must never
  serialise against each other.
- **Transaction-aware.** Eviction must never close a connection that is mid-transaction;
  a checked-out handle is not evictable by construction.
- **Invisible to callers.** `DbConnector` is already an RAII wrapper around `sqlite3*`, so
  the pool goes behind it: existing call sites keep working unchanged.

Deadlock risk to design against: `local_add` acquires the vector db while the caller may
already hold a dirs-db handle. Either enforce a fixed acquisition order, or forbid holding
two handles at once (simpler, and the current code does not need it).

## 4. Why the existing fixes stay

The pool replaces the *structure*; it does not make the current fixes redundant.

- **`sqlite3_busy_timeout(5000)` (`c1a05508`) stays.** Even with one connection per file,
  contention does not disappear — two threads in the DFS pool can still reach the same
  vector, and without a busy timeout sqlite returns `SQLITE_BUSY` *immediately* rather
  than waiting. "Wait for the transaction to finish instead of failing" is exactly what
  the timeout does, one level below the pool. After the pool it should almost never fire —
  which is what a safety net is for.
- **Off-thread writes (`894f11db`) stay, and matter more.** With a pool, a caller may wait
  for another holder to release the file. That wait must not happen on the message
  dispatch thread, or consensus traffic starves behind it — the same failure we fixed by
  moving bulk fragment handling off that thread.
- **The warning on a rejected row stays.** A dropped row was completely silent, which is
  why this ran for hours unnoticed. Whatever the storage layer looks like, losing a chat
  message must leave a trace.

Still open regardless of the pool: a write that genuinely fails is not re-queued, and
`local_add` returns `true` both when it writes and when it skips a stale row, so the
caller cannot tell success from silent discard.

## 5. Verification

The stand already measures this. `audit_balances.py` and the vector row audit compare
row sets across nodes against their union; the metric to move is "rows lost per node"
(459 on the pre-fix run, target 0). Watch fd count under a long run — the point of the
bound is that it stays flat while vector count grows.
