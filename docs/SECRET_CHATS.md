# Secret chats (Signal-like)

Status: specification, nothing implemented yet. Decisions below were taken on 2026-11-23.

## Goal

End-to-end encrypted one-to-one chats where the EXC network only **carries** data. Everything
else (identity material, ratchet state, messages) lives on the device. Nothing goes through
DFS: a DFS write broadcasts `owner_id` and `actor_id` in plaintext, which would publish who
talks to whom even if the payload is encrypted.

## Scope of v1

- Text only, 4 KiB per message. Media and files come later and will not use DFS either.
- One device per secret chat (like Telegram secret chats). No fan-out, no linked devices.
- One-to-one only.

## Cryptography

All primitives are already in libsodium; there is no libsignal in the tree.

- Identity: the per-secret-chat identity key is derived from the profile seed with its own
  label, separate from `chat_main` and from every per-chat actor, so a secret chat cannot be
  linked to the regular chat identity.
- Session start: X3DH with identity key, signed prekey and (later) one-time prekeys.
  v1 publishes identity key + signed prekey only. The first message therefore has weaker
  forward secrecy than with one-time prekeys; the ratchet fixes this from the second message on.
- Session: Double Ratchet. X25519 DH ratchet, symmetric chains keyed with BLAKE2b-based KDF,
  messages sealed with XChaCha20-Poly1305. Out-of-order tolerance with a bounded skipped-key
  window. Message keys are deleted right after use.
- Prekey bundle is public key material and is published in the public `ChatProfile` dictionary
  that already exists. It reveals nothing beyond the profile.
- Verification: a safety number (fingerprint of both identity keys) shown as digits and QR.

## Local storage

Encrypted SQLite on the device: sessions, skipped keys, messages, per-chat settings. The key
is wrapped by the device keystore and by the app passcode when one is set. Losing the device
loses the chats. Expired messages (disappearing timer) are deleted from disk, not hidden.

## Transport: envelopes and mailboxes

A new network message type carries a **sealed envelope**:

- `mailbox_id` = BLAKE3 of the recipient identity public key and a fixed domain label. Nodes
  see an opaque id, never an actor id.
- Sealed sender: the sender identity travels inside the ciphertext, the outer layer carries only
  an ephemeral key.
- `created_at` (signed with the ephemeral key), `ttl` and the ciphertext. Maximum envelope size
  is bounded (v1: 4 KiB payload plus overhead).

Delivery:

1. The sender broadcasts the envelope to **all full nodes**.
2. Every full node stores it for at most `TTL` (7 days) and answers fetches by `mailbox_id`.
3. The recipient connects, asks any full node for its mailbox with a signature proving ownership
   of the mailbox key, decrypts, and acknowledges.
4. An acknowledgement is gossiped and nodes may delete early. This is an optimisation only.

Deleting early is never required for safety:

- Each node expires envelopes independently by `created_at + TTL`. No node waits for another,
  and a node that was offline simply expires old entries when it returns.
- A stale copy is useless: message keys are deleted after use (forward secrecy), the envelope
  contains no sender or content in the clear, and a replayed envelope is rejected by the ratchet
  because its message number was already consumed.
- One online full node is enough for delivery. Availability, not confidentiality, is the weak
  point: a node can refuse to serve an envelope. The sender sees the missing acknowledgement and
  resends.

## Abuse control

Broadcasting to every full node multiplies storage and traffic, so a mailbox is a shared space
in the sense of `POW_ANTISPAM.md`. v1 limits:

- hard cap on envelope size;
- quota of stored envelopes per `mailbox_id` and per sender-ephemeral rate window;
- a light memory-hard proof of work per envelope is the candidate for the cost-of-spam knob;
  it must stay under about 0.5 s on a phone. Not decided yet.

## Phases

0. This document.
1. Library: X3DH, Double Ratchet, local store, safety number. Unit tests with fixed vectors,
   no network.
2. Transport: envelope message type, node-side mailbox with TTL and quotas, fetch and
   acknowledgement. First between two nodes, then on the stand.
3. App: secret chat list and screens, disappearing timer, verification screen, notifications.
4. Media.

## Open

- Proof of work or quota only for envelopes.
- Where the mailbox key is derived and how it is rotated.
- Whether the backend can enforce quotas without being able to link envelopes to senders.
