# Bounded Network Receive Queue

Task 210 moves network reputation SQL off the node's Qt event loop. It does not
batch writes, change SQLite pragmas/schema, change admission
rules, or move the message switch to a worker thread.

## Ordering And Ownership

`NetworkManager::message_received` checks shutdown and the 64-byte signature
boundary before enqueueing an owned copy of the frame, IP and peer identifier.
Only admitted frames reach duplicate accounting and deserialization. The queue
dispatches one item at a time on its QObject owner thread; one dedicated worker
executes deferred SQL stages. The owner thread remains available while SQL waits.

Ready packets share one queued owner wakeup, up to 64 packets or 2 ms between
packet boundaries. A deferred stage stops that drain; the next packet still waits
for every continuation of its predecessor. Reentrant producers cannot extend one
wakeup indefinitely. Slow handlers are not interrupted, so this is a scheduling
quantum rather than a hard bound on handler latency. Capacity, FIFO and SQL
durability remain unchanged; the batching does not qualify full-load performance.

For a message that needs reputation, the order is: obtain current reputation (a cache hit on
the owner, otherwise a read on the worker),
set the responder's read-before-increment value on the owner, log/count traffic,
increment broadcast reputation on the worker, then run the original message
switch on the owner. Focused messages have no increment. Missing-value fallback
and the network-actor multiplier are unchanged. Custom messages do not read or
write reputation, but cannot overtake a previously admitted message.

Actor/Actors/ActorsHash/NewActor and RequestDfsSize/ResponseDfsSize also skip the
unused reputation read. Their reviewed handlers do not consume that value, and
reply routing does not serialize it. Non-Custom broadcasts still perform exactly
one deferred increment before dispatch, including these types; failures and
shutdown retain the same suppression rules. This explicit allowlist does not
include DAG messages or unknown types: their read-before-increment responder
values, fallback and multiplier remain unchanged. No authorization check is
removed, and no accepted frame overtakes another.

The read-through cache retains at most 1024 successful, parsed reputation reads
with keys of at most 256 bytes. At capacity it flushes before inserting another
entry; it is not an LRU. Missing/error/invalid rows are not cached. Owner lookups
use `try_lock` and fall back to the worker on contention; no cache mutex is held
across SQL. Per-node mutations invalidate before and after SQL, including an
exception or nonthrowing write failure. A generation check prevents an older
in-flight read from refilling the cache across invalidation. Expiry invalidates
all entries before/after its query; reset/reinitialization also discard cache
state. Writes are never reflected optimistically. Runtime database mutations
must go through LuminanceManager; out-of-process database edits are not cache
coherent. This does not change the existing lifecycle requirement against
reset/destruction overlapping worker access.

The pending receive owns the decoded body, signature, identifier and responder.
The next packet cannot begin until all stages and callbacks of the current one
have returned. A completion delivered by a nested Qt event loop is retained and
reposted after the outer handler/continuation returns. This also preserves the
borrowed input packet's lifetime during reentrant `stop()`.

Periodic reputation expiry is a FIFO maintenance item on the same worker. There
is at most one pending/active maintenance item. It has one reserved slot so a
full packet queue cannot prevent an expiry request; it cannot overtake accepted
packets. Timer ticks while that item is pending are coalesced.

## Capacity And Failure

Default capacity is 1024 packets and 64 MiB of retained raw frame/IP/identifier
bytes, including the active packet. One coalesced maintenance item is additional.
Admission and capacity accounting are mutex-protected. These limits do not bound
Qt/socket buffers, decoded objects, allocator overhead or total process RSS.

Overflow rejects the new packet before changing duplicate state and closes its
nonempty peer identifier through the existing owner-thread connection remover.
It does not evict earlier accepted packets. This is global backpressure, not a
per-peer fairness or rate-limiting mechanism. Native load qualification must
check queue rejection and connection churn, not only memory use.

Overflow diagnostics include pending item count, retained bytes and rejected raw
frame size, never packet contents or peer identifiers. Count and bytes are
separate observations immediately after rejection, not an atomic admission
snapshot. Pending count includes the reserved maintenance item when present.
Stages taking at least 100 ms report only a fixed stage name, numeric message
type and elapsed monotonic time. Stages are preparation, owner-thread dispatch,
and reputation read/increment/expiry on the worker. Unknown/pre-decode types use
-1. Preparation can include a direct synchronous dispatch; overlapping timings
must not be summed. Deferred queue wait and event-loop delivery delay are not
included in worker execution time. Logging failures are suppressed, including
during exception unwinding. This instrumentation does not change capacity,
FIFO, durability, deadlines or the packet's exception-handling path.
Elapsed wall time includes scheduling delays; it is not a CPU-time measurement
or proof of the operation responsible for contention.

Connection activation requests DFS size only from the activated peer, using
`newSocketActivatedWithParams` and a Focused Request. Previously every activation
polled all neighbours again: N new peers on top of B existing peers generated
N*B + N*(N+1)/2 requests at that node, before replies or reconnects. The scoped
overload rejects an empty peer identifier instead of falling back to fan-out.
The original one-argument API still supports an explicit all-neighbour query.
This reduces activation traffic without dropping replies, changing the wire
format, increasing queue capacity or changing FIFO admission.

Thrown worker/handler exceptions suppress that packet's remaining dispatch and
report a generic error without payloads or credentials. Later packets may proceed.
An exception from the error reporter cannot strand the queue. The existing
Luminance API returns no write status: a nonthrowing failed SQLite query retains
its previous semantics and is not converted into a successful-commit guarantee.

## Shutdown Contract

Enqueue may be called concurrently only while the queue's lifetime is externally
guaranteed. `defer`, `stop` and destruction belong to the QObject owner thread;
deferred operations must not call back into the queue or wait on that thread.
Initialization/reset/destruction of the storage must not overlap worker access.

`ExtraChainNode` joins the receive worker at the start of its destructor and its
thread-finished cleanup, before cleanup callbacks or any manager teardown.
LuminanceManager is not a QObject. It is now explicitly owned by unique_ptr,
replacing an unowned allocation; its database closes after the early join and
before QObject deletes the network-manager child. A QPointer guards the network
manager when deferred cleanup has already deleted it. This corrects ownership;
it is not evidence that the previously leaked storage caused a use-after-free.
`NetworkManager` also destroys the queue first in its own destructor. Stop rejects
new intake, joins the already accepted SQL stage (including a queued increment),
discards not-yet-dispatched packets and suppresses queued continuations. It does
not promise to process the backlog or to turn an accepted read into a new write
after shutdown. The existing node-enabled checks suppress stages/dispatch that
have not started. No new dependence on the shared Boost pool is introduced.

Stop can wait for the underlying synchronous SQL operation; it is not a bounded
shutdown deadline. No journal mode, fsync policy or power-loss guarantee changes.
Lifecycle-only direct storage callers must retain their existing lifetime rules.

## Tests And Qualification

With `EXTRACHAIN_BUILD_DB_TESTS=ON`, build/run `extrachain-receive-tests` as well as
`extrachain-db-tests`. Queue tests use real Qt events and a dedicated worker;
reputation order/shutdown tests use the real LuminanceManager and temporary SQLite.
Cache regressions cover missing rows, successful/failed writes, expiry, reset,
bounded entries/keys and isolation between peers. Extracted production setup
tests cover cache-hit responder values, deferred broadcast increments and shutdown.
`network_dfs_size_fanout.py` compiles the actual activation callback and senders
with a routing observer: one/100 peers, reconnect, empty target, explicit global
query and QObject-context wiring. This is not a full socket transport test.
They cover owned input, FIFO, active/metadata capacity, owner-loop progress,
read/commit/dispatch order, maintenance, shutdown, exceptions, self-deletion,
concurrent intake and immediate idle shutdown. Two nested-event-loop tests failed
before the reentrancy fix and pass after it; reentrant stop is also covered.
The compiled `network_receive_timing.py` fixture uses the actual timer class with
a controlled monotonic clock and log observer. It covers the exact threshold,
type updates, early return, unwinding and throwing log sinks. Admission and
reputation fixtures stub timing, whose output does not alter their contracts.
The parent-ownership regression uses a real Qt child queue and owned real SQLite
storage, then verifies commit visibility after the storage is destroyed.
`network_receive_shutdown.py` compiles the actual node shutdown bodies with
dependency-order observers: destructor and thread-finished cleanup failed before
the early join, while uninitialized-node destruction already passed. This checks
wiring, not a complete running-node teardown or proof of a historical crash.

`network_receive_frame.py` and `network_custom_reputation.py` compile extracted
production fragments with controlled queue/DB/decoding observers. They check
admission-before-cache, shutdown boundaries, unchanged Custom behavior and
non-Custom responder semantics. They are not full-node or socket integration tests.
The setup observer also covers the read-free allowlist for all three statuses,
focused/broadcast modes, write failure and shutdown; unlisted messages keep the
read/increment/dispatch sequence. These tests do not replace review of a handler
when its use of responder reputation changes.

These focused tests do not qualify complete application shutdown, platform
packaging, the 100-client workload or a latency improvement. In particular,
ActorIndex writes in the message switch still run on the owner thread. Task 210
requires independent runtime qualification; full-workload results are kept in
the owner-authorized local report, not inferred from the queue tests.
