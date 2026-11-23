/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

// End-to-end session of a secret chat: X3DH start and a Double Ratchet with encrypted headers.
// Depends on libsodium only. Specification: docs/SECRET_CHATS.md.
//
// Wire messages reveal nothing a relaying node could link: the ratchet header (public key,
// counters) is encrypted, the plaintext is padded to fixed steps, and the mailbox id of every
// message is derived from a secret, so no two messages share an id.
namespace SecretChat {

using Bytes     = std::vector<std::uint8_t>;
using Key32     = std::array<std::uint8_t, 32>;
using SignKey   = std::array<std::uint8_t, 64>;
using Signature = std::array<std::uint8_t, 64>;
using MailboxId = std::array<std::uint8_t, 16>;

enum class Error {
    InvalidBundle,
    BadSignature,
    BadFormat,
    AuthFailed,
    TooManySkipped,
    TooLarge,
    CryptoFailure,
    // The side that answers cannot send before it has received the first message
    NotReady,
};

// Largest plaintext of one message. The padded plaintext is one of a few fixed sizes.
inline constexpr std::size_t MaxPlaintextSize = 4095;
// Messages that may be lost or reordered inside one chain before the session gives up.
inline constexpr std::size_t MaxSkip = 100;
// Skipped message keys kept for late messages; the oldest go first.
inline constexpr std::size_t MaxStoredSkipped = 1000;
// Ids of each kind a recipient watches ahead of the next expected message.
inline constexpr std::size_t MailboxWindow = 64;

// Long-term Ed25519 identity. The X25519 key used for key agreement is derived from it.
struct IdentityKeys {
    Key32   public_key {};
    SignKey secret_key {};

    static std::expected<IdentityKeys, Error> from_seed(const std::array<std::uint8_t, 32> &seed);
};

// What a person publishes so others can start a session: identity key and a signed prekey.
struct PreKeyBundle {
    Key32         identity_key {};
    Key32         signed_prekey {};
    std::uint32_t signed_prekey_id = 0;
    Signature     signature {};

    bool                                      verify() const;
    Bytes                                     serialize() const;
    static std::expected<PreKeyBundle, Error> parse(std::span<const std::uint8_t> data);
};

// The private half of a published signed prekey. Kept on the device until it is rotated out.
struct SignedPreKey {
    std::uint32_t id = 0;
    Key32         public_key {};
    Key32         secret_key {};
    PreKeyBundle  bundle;

    static std::expected<SignedPreKey, Error> generate(const IdentityKeys &identity, std::uint32_t id);
};

// Id under which the first message of a new session is delivered to the owner of a signed prekey.
MailboxId initial_mailbox_id(const Key32 &signed_prekey);

// 60 digits in groups of five, equal for both sides, to compare out of band.
std::string safety_number(const Key32 &identity_a, const Key32 &identity_b);

struct SessionState;

class Session {
public:
    struct Started;
    struct Accepted;

    // Starts a session towards the owner of a bundle and seals the first message to its signed prekey.
    static std::expected<Started, Error> initiate(const IdentityKeys              &self,
                                                  const PreKeyBundle              &peer,
                                                  std::span<const std::uint8_t>    first_plaintext);
    // Opens the first message of a session addressed to one of our signed prekeys.
    static std::expected<Accepted, Error> accept(const IdentityKeys           &self,
                                                 const SignedPreKey           &prekey,
                                                 std::span<const std::uint8_t> wire);

    std::expected<Bytes, Error> encrypt(std::span<const std::uint8_t> plaintext);
    std::expected<Bytes, Error> decrypt(std::span<const std::uint8_t> wire);

    // Id the next message we send must be delivered under; empty until the session can send.
    std::optional<MailboxId> next_send_mailbox_id() const;
    // Ids under which messages of the peer may arrive; any other id is not ours to read.
    std::vector<MailboxId> expected_receive_mailbox_ids() const;

    const Key32 &peer_identity() const;
    std::string  safety_number() const;

    Bytes                                serialize() const;
    static std::expected<Session, Error> deserialize(std::span<const std::uint8_t> data);

    Session(const Session &);
    Session &operator=(const Session &);
    Session(Session &&) noexcept;
    Session &operator=(Session &&) noexcept;
    ~Session();

private:
    explicit Session(std::unique_ptr<SessionState> state);
    std::unique_ptr<SessionState> state_;
};

struct Session::Started {
    Session session;
    Bytes   wire;
    // Delivery id of `wire`
    MailboxId mailbox_id {};
};

struct Session::Accepted {
    Session session;
    Bytes   plaintext;
};

} // namespace SecretChat
