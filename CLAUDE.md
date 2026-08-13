# CLAUDE.md

## Project
ExtraChain Core — C++23/Boost library: DAG ledger + DFS file system. Qt is an optional client
compatibility layer.
- License: LGPL v3
- Branch for PRs: dev (current work usually on feature branches)
- Build/deps: see README. vcpkg, CMake.
- Open work: `docs/TODO.md`. Testing rules: `docs/TESTING.md` (a core change is validated
  on a combined DAG + DFS stand — both at once).

## Critical Code Rules

### ActorId / BigNumber / BigNumberFloat
Use static `create()` factory, not direct constructor — validates input.

### Precompiled headers
`headers/precompiled.h` includes std, Boost, fmt, magic_enum, and msgpack. Qt code must include
its Qt headers directly and must stay in the compatibility target where possible.
Don't re-include in source files. clangd shows false-positive errors because PCH not visible to LSP — ignore.

### Logging
`eLog()`, `eWarning()`, `eCritical()`, `eFatal()`, `eTemp()`. Never qDebug.

### Comments
Default: no comments. Add only when WHY is non-obvious. Never explain WHAT.

### `std::expected<T, E>` everywhere
Standard error pattern, no exceptions:
```cpp
std::expected<Foo, FooError> bar();

auto result = bar();
if (!result.has_value()) return std::unexpected(result.error());
auto& foo = result.value();
```
Always `.has_value()` / `.value()`. Don't use `*` or `->` on optional.

### `std::optional<T>`
Same: `.has_value()` / `.value()`. JSON serializer skips nullopt fields automatically.
`value_or(default)` only when truly needed.

### Json
`Json::serialize(struct)` / `Json::deserialize<T>(string)` from `utils/exc_utils.h`.
Returns `expected<T, std::string>`. Works on any BOOST_DESCRIBE-enabled struct.

## Architecture

### ExtraChainNode
`ExtraChain::Core::ExtraChainNode` is the Core entry point. It instantiates managers in order and
owns the lifecycle, Boost runtime, serial executor, and periodic tasks. The global
`ExtraChainNode` is the Qt compatibility facade.

### Layers
- **chain/**: Dag (DAG ledger, Full/Light modes), Section (block-like), Transaction, DagCache (balance cache, CACHE_LAG_SECTIONS = 15 behind current), ActorIndex (network actor registry).
- **encryption/**: Actor<KeyPrivate|KeyPublic>, libsodium, Ed25519, ActorId = blake3(public_key)[:40].
- **network/**: NetworkService (Boost.Beast WebSocket P2P), Responder pattern (deferred responses),
  NetworkManager Qt facade, ~54 MessageType enum values.
- **dfs/**: DfsService (Full/Light), DfsController Qt facade, Vector/Dictionary/Collection
  templates, fragment-based downloads (256KB), DataSecurity {Public, Encrypted, Self, Actor, Key}.
- **managers/**: AccountController, LuminanceManager, TokenManager, DataMiningManager, ChatManager, JanusManager, ThothManager.

### Account / Profile
- `AccountController`: profiles (Old / New with seed phrase). Each profile has system + main actors, plus optional wallets and chat_main.
- `PrivateProfile.actors_`: keypair list. DFS signer lookup uses this — every signing actor must be here.
- `SeedProfile`: seed phrase persistence. `generate()` derives system, main, chat_main; `generate_other()` derives wallets by index.

### Chat (per-chat actors model)
- **chat_main** (per profile): derived from seed with label `"chat"`, single identity for chat features. Holds ChatProfile (name, bio, avatar) in public DFS Dictionary.
- **per-chat actor** (per dialog/channel): derived from seed with label = hex(chat_key). Owner of chat DFS vector, signs participant's messages, restored on login via `AccountController::restore_actor()`.
- **MyChatsInfo**: DFS Dictionary under chat_main, self-encrypted, stores `Chat::Chat` records (chat_key, my_per_chat_id, peer_chat_main_id, ...). Optional fields (`std::optional<>`) skipped in JSON.
- **ChatProfile**: DFS Dictionary under chat_main, public. Keys: name, bio, avatar (JSON ChatProfileAvatar with full_id, mini_id, blur_hash).
- **Invites**: stored in peer's DFS space at folder `:DApp:Chat:Invite`, Actor-encryption. Signed/encrypted by the **per-chat actor** (NOT chat_main) so the replicated DirRow author stays anonymous; payload carries `sender_chat_main_id` verified via `bind_signature` so recipient still learns who wrote. `parse_invite` checks `dir_row.actor_id == sender_per_chat.id()` and verifies bind against chat_main from actor_index.
- **Activation gate**: `ChatManager::set_mode(ChatMode::Enabled)` + `activate()` (called from firstSyncEnded). Without this, chat operations are no-ops.
- **Privacy goal**: main (real id, holds balance) is never exposed in chat. chat_main hides main; per-chat actor hides chat_main from outside observers (different per chat). Old profiles have no chat — `chat_actor()` returns `NoSeed`, never falls back to main.
- **DirRow metadata leak (key constraint)**: `Dfs::DirRow` is broadcast network-wide (`broadcast_stored` / `MessageType::DfsStoreFile`) with `owner_id` + `actor_id` in PLAINTEXT (only file payload is encrypted). Any DFS write reveals who-wrote-where → this drives all invite-delivery privacy design. See `POW_ANTISPAM.md` in repo root for the deferred full fix.
- **Message types** (`Chat::MessageType`): Text=0, Created=1, Invite=2, Join=3, Image=4, Gif=5, Audio=6, Voice=7, Video=8, File=9. Every chat/channel has a system Created(1) — UI must not count it as a real message.

### DFS Templates
- `CollectionTemplate::create(name).use_id()` — struct must have `std::string id` field, generate manually: `id = Utils::generate_random_hex(6)`.
- DataSecurity::Self: encrypts file/vector names. Use `find_file_self()` to search among encrypted names. Same name in different folders is allowed.

### Synchronization Flow
1. Peer connects → ActorIndex sends system actor, requests actor hash.
2. `ActorIndex::firstSyncEnded` — actor index synced.
3. `dogenerate()` — restore wallets from seed.
4. `chat_manager_->activate()` — broadcast chat_main, scan invites.
5. `dag_->start_check()` — chain sync.
6. `dfs_->sync()` — DFS sync per identifier.

### Transaction Flow
**Send**: AccountController signs → `Dag::prepare_transaction()` → `send_transaction()` → broadcast.
**Receive**: NetworkManager DagTransaction msg → `Dag::network_transaction()` → `prove_transaction()` (sig + balance + duplicate) → `save_transaction()` or `network_transaction_result()` with error.

## Common Patterns

### Creating actor (random)
```cpp
Actor<KeyPrivate> actor;
actor.create(ActorType::User);  // or Service / DAppMaster
```

### Creating actor (from seed)
```cpp
account_controller->create_actor(profile_actor, seed_label, type);  // label-based, broadcasts if new
account_controller->restore_actor(profile_actor, seed_label, type); // local only, no broadcast
```

### DFS write/sign
DFS finds keypair via `current_profile().get_actor(signer_id)` — signer must be in `profile.actors_`.

### Thread pool
```cpp
ThreadPoolBoost::thread_pool.submit([]() { /* bg work */ });
```

### JanusManager
Inherit from `JanusBidBase` / `JanusItemBase`, BOOST_DESCRIBE_STRUCT, then:
- `janus_manager->create_bid_template(name, def)`
- `janus_manager->create_item_vector(name, template_owner, template_name)`
- `janus_manager->place_bid<MyBid>(owner, item_file, bid)`

## Files Often Touched
- `core/extrachain_node.{h,cpp}` — Core init, manager wiring, lifecycle, and events.
- `managers/extrachain_node.{h,cpp}` — Qt node facade and signal conversion.
- `chain/dag.{h,cpp}`, `chain/dag_cache.{h,cpp}` — chain logic.
- `network/network_service.{h,cpp}` — protocol routing and network state.
- `network/network_manager.{h,cpp}` — Qt network facade.
- `dfs/dfs_service.{h,cpp}` — DFS API and state.
- `dfs/dfs_controller.{h,cpp}` — Qt DFS facade.
- `chat/chat_manager.{h,cpp}` — chat features.
- `managers/account_controller.{h,cpp}` — profile/actor lifecycle.

## Scripts
- `scripts/clang_format.js` — format code (clang-format runner).

## Commits

- **One tag, one line.** Subject is `[Tag] What changed`, a single line, imperative,
  no trailing period. Use `[Core]` when the change spans subsystems — never stack tags
  like `[Network][Dfs]`. Single-subsystem changes keep their own tag (`[Dag]`, `[Dfs]`,
  `[Network]`, `[Chat]`, `[Docs]`, `[Tests]`).
- **No body unless it earns its place.** A body is for a non-obvious root cause or a
  constraint the code cannot express. Do not restate the diff.
- **Do not create merges from a work branch.** Commit straight onto the target branch.
  A test-stand worktree is a place to build and run, not a source to merge from — carry
  the change over and commit it directly, so the stand leaves no trace in product history.
  `[Merge]` is only ever valid for a genuine integration of two real branches.
- **No trailers.** No `Co-Authored-By`, no `Generated with`, no session links.
- Test-stand hacks (env switches, port/address overrides, rate-limit bypasses) never
  appear in a commit — strip them before committing, restore them afterwards.
