# DFS File-State Dispatch

`DfsController::network_response_file_state` receives network responses on the
node event loop. Its metadata lookup can wait behind a writer using the same
connection, even with connection-local locking. Preparing a download also reads
metadata and can hash stored content. A captured runtime stack established this
path as one source of VPN control-loop stalls, not the cause of every delay.

Ready responses with an origin now post this work to the existing DFS executor,
as other DFS response handlers already do. The packet and selected origin are
copied before dispatch; the task does not retain the `Responder`. Non-ready
responses and empty origins do not read metadata or allocate an executor task.
Missing metadata remains a no-op. Successful processing keeps the stored row's
metadata, applies the announced state/hash and uses the existing download queue.
Standard exceptions are contained at the worker boundary with a fixed warning
that does not log packet contents or exception details.

## Lifetime And Scope

The handler checks `node_enabled` before dispatch, before accessing controller
state and after the potentially blocking metadata lookup. It uses the existing
node-owned task lifetime contract: `ExtraChainNodeWrapper` disables work and
terminates the thread pools before destroying the node. The checks alone are
not a lifetime barrier and do not make arbitrary concurrent controller deletion
safe. No new executor or ownership framework is introduced.

SQLite locking, durability, transactions, DAG synchronization, download policy,
VPN authentication and cleanup leases are unchanged. Other synchronous DFS
handlers and executor backlog can still cause delays and require separate
evidence. This change is not a claim that all event-loop stalls are resolved.

## Verification

Run `python3 tests/dfs_file_state_dispatch.py` with a C++20 compiler; set
`RACCOON_TEST_CXX=cl` in an MSVC developer environment on Windows. The fixture
extracts and compiles the actual handler with controlled database, download
queue and executor boundaries. It uses a real worker thread for execution but
does not instantiate the real Boost executor, Qt node or a retained profile.

Nine cases cover deferred reads/queue work and copied inputs, non-ready and
removed responses, empty origin, stopped node, shutdown before execution and
during lookup, missing metadata, and a contained worker exception. All nine
failed against the original synchronous method and passed after the change on
Linux and MSVC. The complete Windows application also built successfully and
the existing actual SQLite regression target reported nine passes. Build and
fixture results do not replace deployed Linux/Android runtime qualification.
