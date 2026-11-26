# Application-controlled P2P suspension

`ExtraChainNode::set_network_suspended(bool)` must run on the node's Qt thread.
It can be called before `ExtraChainNodeWrapper::init()`. Suspension is per node
and atomic for callers reading `network_suspended()` from other threads.

The synchronous barrier closes the WebSocket listener, aborts connected sockets
and pending TCP probes. Connection creation, reconnection, handshakes, public-key
exchange, receive processing and queued sends all check the suspended flag.
Probe timers belong to their sockets, so tearing down a probe also cancels its
timeout callback. Already transmitted bytes cannot be retracted; privacy-sensitive
clients must select suspension before initialization and retain it across tunnel
changes. Suspension also pauses chain and DFS synchronization over these links.

The barrier covers ExtraChain's P2P transport, not arbitrary application HTTP
requests. A VPN client must separately restrict control routes and avoid actor
metadata in its admission protocol. Re-enabling P2P is an explicit owner-thread
operation; it restarts the listener and normal reconnection policy.

`AccountController::profileChanging` fires synchronously before replacing,
clearing or deleting a selected profile. Listeners may invalidate asynchronous
authorization immediately, then queue work to their own threads. It also fires
on initial profile load: applications preserving an independent native VPN
service must distinguish that load from replacement of a previously selected
profile and verify their locally persisted owner binding after load.

`extrachain-websocket-tests` exercises suspension with real loopback WebSockets,
suppressed handshakes/sends/listeners, and pending-probe timer destruction.
