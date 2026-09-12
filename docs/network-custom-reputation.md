# Custom Message Reputation Scope

`NetworkManager::message_received` does not read reputation for `Custom` messages.
That branch passes only the package and decoded custom payload to the application,
or forwards the package. Its local `Responder` is unused and has no destructor
side effects. `read_luminance` is a lookup, not a reputation update.

Other message types retain the same lookup, missing-value fallback, network-actor
multiplier and read-before-broadcast-increment order. Custom broadcasts already
excluded reputation increments. Deduplication, decoding, traffic accounting,
delivery/forwarding and the existing authentication path are unchanged.
This removes an unnecessary database dependency, not all network event-loop
blocking. It does not introduce caching, async writes or weaker durability, and
does not supply cryptographic authorization for claimed actor identities.

Run the focused C++20 regression with:

```sh
python3 tests/network_custom_reputation.py
```

On Windows use a Visual Studio developer environment with
`RACCOON_TEST_CXX=cl.exe`. The test compiles the actual `Responder` definition,
receive setup, broadcast-increment condition and Custom branch. Database,
MessagePack decoding and delivery endpoints are observers; unrelated handlers
are not compiled. Six scenarios cover local/forwarded/invalid custom payloads,
unavailable-store access, focused/broadcast non-Custom controls and interleaving.
Controls exercise missing/zero/positive reputation and the network multiplier.
This is not a full socket/Qt/SQLite or platform VPN workflow qualification.
