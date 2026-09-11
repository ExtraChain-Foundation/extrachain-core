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

The only production opt-in is `LuminanceManager`: network message dispatch reads
reputation synchronously, so waiting for unrelated database operations also stalls
VPN control messages. This change does not bypass reputation checks, cache stale
values, alter authorization, or change DAG synchronization.

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
