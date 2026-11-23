# Secret chats (Signal-like)

Status: specification, nothing implemented yet. Decisions of 2026-11-23.

## Goal

End-to-end encrypted one-to-one chats where the EXC network only **carries** data. Everything
else (identity material, ratchet state, messages) lives on the device. Nothing goes through
DFS: a DFS write broadcasts `owner_id` and `actor_id` in plaintext, which would publish who
talks to whom even if the payload is encrypted.

## Scope of v1

- **Online only.** A message is delivered when the recipient is reachable at that moment.
  No store-and-forward on nodes (see "Later" for the mailbox).
- Text only, 4 KiB per message. Media and files come later and will not use DFS either.
- One device per secret chat (like Telegram secret chats). No fan-out, no linked devices.
- One-to-one only.

## Threat model

- Every relaying node sees the ciphertext in transit and a dishonest one can keep it forever.
  Deletion by a node is never part of the security argument.
- Security therefore rests on cryptography alone: Double Ratchet gives forward secrecy
  (message keys are deleted after use) and post-compromise recovery (each DH step heals).
  A recorded blob without the keys is useless, and a stolen key opens one message, not the
  history.
- What a recording node must not learn from a blob: the recipient, the sender, the link
  between two messages, the length. Those are format requirements below.
- Known weak spots, accepted for v1 and listed so they are not forgotten:
  - first message without one-time prekeys has weaker forward secrecy until the first reply;
  - X25519 is not post-quantum, so blobs recorded today can be opened by a future quantum
    computer. Signal added a post-quantum key agreement (PQXDH) for this reason.

## Cryptography

All primitives are in libsodium; there is no libsignal in the tree.

- Identity: the per-secret-chat identity key is derived from the profile seed with its own
  label, separate from `chat_main` and every per-chat actor, so it cannot be linked to the
  regular chat identity.
- Session start: X3DH with identity key, signed prekey and (later) one-time prekeys. v1
  publishes identity key + signed prekey only, and the signed prekey is rotated.
- Session: Double Ratchet. X25519 DH ratchet, BLAKE2b-based KDF chains, XChaCha20-Poly1305
  messages. Bounded skipped-key window for out-of-order delivery.
- Prekey bundle is public key material, published in the public `ChatProfile` dictionary that
  already exists. It reveals nothing beyond the profile.
- Verification: a safety number (fingerprint of both identity keys) as digits and QR.
- The wire format reserves a slot for a second, post-quantum key encapsulation so it can be
  added without a format break. libsodium has no such primitive; a separate library is needed.

## Local storage

Encrypted SQLite on the device: sessions, skipped keys, messages, per-chat settings. The key
is wrapped by the device keystore and by the app passcode when one is set. Losing the device
loses the chats. Expired messages (disappearing timer) are deleted from disk, not hidden.

## Sharing the network with other applications

`Custom` messages are the application channel of the network and the VPN application uses
them too. Without a namespace a node of one application hands the other's payloads to its
own parser and drops what it cannot read, which breaks relaying through it. `CustomMessage`
therefore carries an `app` tag:

- Empty tag: the legacy untagged form, handled exactly as before (the VPN keeps working with
  no change).
- Tagged message: `NetworkManager::register_custom_app(tag, relay)` claims a tag. A claimed
  tag is delivered to `customMessageReceived` (and relayed when `relay` is set); an unclaimed
  tag is relayed untouched and never reaches another application's handler.
- Secret chat claims `CustomAppSecretChat` (`"secret-chat/1"`) with relay on.
- Wire compatibility: the field is the last element of the MessagePack array. An old node reads
  only `owner` and `data` and ignores the rest; a new node reading an old message sees an empty
  tag. Checked in both directions. An old VPN node still hands a tagged message to its own
  handler and may not relay it, so secret envelopes should not rely on a path made only of such
  nodes until the VPN side skips tags it does not know.

### Older applications must not be disturbed

Applications released before secret chats stay on the network and must neither crash nor act on
secret traffic. The rule for the `data` of a secret chat `CustomMessage` is: it is exactly one
MessagePack binary value (`custom_payload`). The VPN application, for example, reads `data` as
one of a few array structs, a map or a boolean; a binary value is none of them, so each parser
throws an exception the reader already catches, and the reader logs the packet as not its own
and returns. A wrapper in the shape of an array is not safe: MessagePack accepts an array with
fewer elements than the struct, so short payloads were mistaken for a message of the other
application in a test. `tests/secret_custom_compat.cpp` runs 100 000 random envelopes through
parsers shaped like those of the VPN with the real MessagePack library and checks both
directions of the tagged frame. An older node that does not relay what it cannot parse is a
loss of one path, not a fault.

## Transport in v1: sealed envelope, relay only

A new network message type carries a **sealed envelope** (`SecretEnvelope`):

- `mailbox_id` is derived **per message** from the ratchet state of the pair, so two messages of
  one person never share an id and a node cannot link them or learn the recipient. The recipient
  computes the ids it expects.
- Sealed sender: the sender identity is inside the ciphertext; the outer layer carries only an
  ephemeral key.
- The payload is padded to fixed size steps, so the length does not leak.
- A short `created_at`, signed with the ephemeral key, bounds replay on the wire. Replay is
  also rejected by the ratchet (the message number is already consumed).

Delivery:

1. The sender floods the envelope to the connected nodes. Nodes forward it like any broadcast
   and **do not store it**.
2. The recipient's app checks incoming envelopes against the ids it expects, decrypts, and
   sends an acknowledgement envelope the same way.
3. No acknowledgement: the sender keeps the message in a local encrypted queue and resends with
   backoff, up to a limit shown to the user as "not delivered".

Presence and delivery state:

- There is no presence registry and no "is X online" query: a query would tell nodes who looks
  for whom. Online is proven only by an acknowledgement that arrives.
- Message states: sending, delivered (acknowledgement received), optionally read. Without an
  acknowledgement the sender resends with growing pauses (for example 10 s, 1 min, 5 min) up to a
  limit, then shows "not delivered".
- Hello: when a device connects to the network it sends each secret contact a short ratchet
  message "I am here". A contact that receives it flushes its queue to that person at once, so
  delivery only needs the two online windows to overlap, whichever side connects second. Relays
  see an opaque envelope like any other. The contact learns that the sender is online, so this
  is a setting; with it off no hello is sent and the queue goes out on the timer alone.
- Messages that cannot be delivered before the windows overlap wait in the sender's local
  queue. Removing that limit is what the later mailbox is for.

Abuse control in v1 is by size and rate only: a hard cap on the envelope, and the existing
relay rate limits. Nothing is stored on nodes, so storage cannot be filled.

## Later: mailbox for offline recipients

Not part of v1. Decisions taken in discussion, to be revisited:

- Full nodes keep envelopes for at most a TTL (7 days) and serve them by `mailbox_id` to a
  recipient who proves ownership of the mailbox key. Delivery goes to **all** full nodes, so one
  online node is enough.
- Every node expires envelopes by `created_at + TTL` on its own, with no coordination, and a
  node that was offline expires old entries when it returns. An acknowledgement gossiped to the
  network lets nodes delete earlier; this is an optimisation only.
- Because a dishonest node can keep blobs, the per-message ids, padding and the post-quantum
  slot above are required before this is enabled, not optional.
- Cost: storage and traffic multiply by the number of full nodes, so a mailbox is a shared
  space in the sense of `POW_ANTISPAM.md`. Candidates: envelope size cap, quota per mailbox id
  and per sender window, a light memory-hard proof of work per envelope (under about 0.5 s on a
  phone). Not decided.
- Alternative with fewer copies: only K nodes chosen by hash of the id or by where the
  recipient is connected. It lowers the exposure but not to zero and adds trust in those nodes'
  availability.
- Open: how the mailbox key is rotated; whether the backend can enforce quotas without being
  able to link envelopes to senders.

## Phases

0. This document.
1. Library: X3DH, Double Ratchet, per-message ids, padding, local store, safety number.
   Unit tests with fixed vectors, no network.
2. Transport: `SecretEnvelope` message type, relay without storage, acknowledgement and the
   sender queue. First between two nodes, then on the stand.
3. App: secret chat list and screens, disappearing timer, verification screen, notifications.
4. Media.
5. Mailbox for offline recipients, post-quantum hybrid.
