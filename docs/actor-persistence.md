# Actor Persistence

ActorIndex persists a received actor vector in one SQLite transaction. Initial
sync keeps its accumulated batch; later responses no longer commit each actor
separately. Single-actor callers retain AlreadyExists and success-signal behavior.
All writes and in-memory publication remain synchronous on the owning thread.

The actor-specific recursive lock covers BEGIN, file creation, inserts, COMMIT
and publication of the committed count and synchronization IDs. It does not use
the unrelated global database lock. Signals follow the successful transaction.
Initial sync does not become ready, discard its pending map or report committed
progress when persistence fails. A smaller peer count cannot bypass that pending
map. Progress is reported after commit, not for uncommitted rows.

Each new file is fully written and flushed to a temporary file in its destination
directory before rename. Existing files are not overwritten. On a known failed
transaction, only files created by that operation are removed; pre-existing
actors are retained. Failed rollback retains files and reports a critical error.
Successful rollback allows a later retry rather than stranding a new actor behind
an AlreadyExists result. Existing orphaned files from older versions are not
automatically repaired by this change.

This does not make SQLite and multiple filesystem files one crash-atomic store,
change SQLite durability settings, move storage off the owner thread, or certify
100-client capacity. Crash reconciliation and bounded asynchronous actor storage
remain separate work. An accepted batch completes before returning, rather than
abandoning a partially committed batch when the node-enabled flag changes.

The native DbConnector tests exercise real ActorIndex, Qt files and SQLite: one
commit for 100 actors, duplicate idempotence, committed state at signal delivery,
BEGIN/INSERT/COMMIT and file-open failures, rollback preserving existing actors,
retries, pending initial sync and reopen visibility. The rejected single-actor
commit regression failed before the change because records() advanced on failure.
The network_actor_persistence.py source-fragment fixture checks response routing,
initial commit failure/retry and the smaller-peer readiness guard with dependency
observers; it is not a socket or full-node integration test.
