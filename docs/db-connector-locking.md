# DbConnector Lock Scope

The default `Shared` scope preserves the existing connector-wide recursive lock.
Do not replace it globally without qualifying multiple connections to the same
database, transactions, busy handling, iterators and the strict connector.

`Connection` scope is an explicit opt-in for a database with one process-local
connector owner. It preserves recursive serialization of that connector's SQL
operations while allowing unrelated databases to progress. Moving the connector
also transfers its mutex. A SQLite build with threading disabled is rejected.
The application must not select SQLite's single-thread startup mode; see the
[SQLite threading contract](https://www.sqlite.org/threadsafe.html).

Production opt-ins are `LuminanceManager` and the DFS metadata connector created
by `DirsSpace::database()` for `DirsManager`. Network message dispatch reads both
reputation and DFS metadata synchronously, so unrelated database operations can
stall VPN control messages. `DirsManager` creates the metadata connection once;
DFS consumers share that same connector through `get_db_instance()`. Do not open
another independent metadata connection while that owner is alive. Collection
databases keep their existing shared scope because independent connectors may
access the same collection. Neither opt-in bypasses checks, caches stale values,
changes authorization, or changes DAG synchronization.

This scope does not make concurrent connector destruction, `open`/`close`, escaped
raw SQLite handles or returned iterators safe. Their existing lifetime contracts
still apply. Do not opt multiple connectors to the same file into this mode.

## Regression Tests

Configure the application/core build with `-DEXTRACHAIN_BUILD_DB_TESTS=ON`, then
build and run `extrachain-db-tests`. The test uses temporary SQLite databases and
a bounded SQL function gate, not node profiles or a live network.

Coverage: unrelated connection progress, serialization within one connection,
unchanged default shared locking, recursive bound operations and a moved
connector. The baseline shared-lock implementation fails unrelated progress.
Native application/load qualification remains separate from these unit tests.

The actual DFS factory is additionally tested against an unrelated blocked SQL
operation, plus concurrent consumers of its one shared connector. The first
case reproduces the old metadata stall; the second retains serialization. Tests
create only temporary databases, restore their working directory and never
instantiate a node or touch a retained profile. Captured runtime stacks establish
the blocked metadata path, not the cause of every slow VPN connection. Collection
hash reads can still wait on the default lock and need separate investigation.

File-state request metadata reads are dispatched through the existing DFS pool,
as are file-state response preparation tasks. The request owns its actor/file
arguments and full responder context across dispatch, reads fresh state under
the unchanged connector lock and suppresses replies when shutdown has begun.
The node wrapper joins the pool before destroying node-owned managers. This
keeps a queued metadata write from blocking this request handler on the network
event loop; it does not remove contention within the metadata connection or
claim that every other synchronous metadata caller has been converted.

`tests/dfs_file_state_request.py` compiles the actual request handler with
controlled executor/DB boundaries. It covers deferred reads, copied reply
correlation and recipients, known/removed/unknown state, multiple pending request
ownership, shutdown boundaries and contained read/send exceptions. Native
workload qualification remains separate from this source-level regression.
