# TODO

## Pack sync (Phase 13)
- Pack-level chain validation on receive: now we trust Pack::Reader::open
  (magic + version + bounds + Blake3 footer checksum). Need to add per-tx
  signature verification once received from a peer — either eagerly during
  receive or lazily on first read. Today: skipped, peer is trusted.
- Peer selection: today we keep using whichever peer the existing sync flow
  picked (Focused responder). Future: switch peer on bad pack response,
  prefer high-luminance peers, parallelize fetch across multiple peers.
- Limit on pack size accepted from peer (DoS guard).
- Per-peer pack request rate-limit (paired with luminance penalty on bad packs).

## Network
- `failed_ips_` — IPs banned forever, never cleared (logic bug)
- UPnP — `makeTunnel` commented out, v2 connector hardcodes port 8080, need to wire up `ws_port`

---

# TODO: Security

## Critical

### Integer underflow in message_received
- **File:** `sources/network/network_manager.cpp:1017-1018`
- If `message.size() < 64`, `message.size() - 64` wraps around (unsigned)
- Fix: add `if (message.size() < 64) return;` before substr

### Weak default salt in key_from_password
- **File:** `sources/encryption/encryption_tools.cpp:52`
- When salt is empty, fills with '0' — same password = same key for all users
- Fix: use `randombytes_buf()` to generate random salt, store alongside encrypted data

### Unchecked first_nodes_[0]
- **File:** `sources/network/network_manager.cpp:64-70`
- Accesses `first_nodes_[0]` without checking `.empty()`
- Fix: add empty check

## High

### SQL: review data flow for network-sourced values
- DFS functions use `fmt::format` for SQL queries
- Most values are internal, but verify that `owner_id`, `file_id`, `name` from network peers
  don't flow into `dfs_utils.cpp` queries without sanitization
- Key places: `dfs_vector.cpp:251` (where_statement), `dfs_utils.cpp:146` (post_query)

### Validate network_id from peer via genesis block
- **File:** `sources/network/isocket_service.cpp:132-133`
- When `our_network_id.is_zero()`, first peer sets network_id without verification
- Should verify against genesis block (section 0)

### Rate limiting
- No per-peer or per-IP rate limiting on network messages
- Can cause resource exhaustion under flood

### Unsafe reinterpret_cast without size check
- **File:** `headers/dfs/dfs_utils.h:50,66`
- `byteArrayToType` / `stdStringBytesToType` don't verify `value.size() >= sizeof(T)`
- Fix: add size check before cast

### Iterator UB in dfs_controller
- **File:** `sources/dfs/dfs_controller.cpp:2084`
- `(virtualFilePath.end()--)->string()` — post-decrement returns old end(), UB
- Fix: use `virtualFilePath.filename()` or `*std::prev(virtualFilePath.end())`
