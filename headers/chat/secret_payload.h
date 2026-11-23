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

#include "chat/secret_session.h"

#include <string>
#include <vector>

namespace SecretChat {

using MessageId = std::array<std::uint8_t, 8>;

MessageId new_message_id();
bool      is_zero(const MessageId &id);

// What travels inside the encrypted body of one message. The kinds that matter to the person are
// tracked: they carry an id, are acknowledged and are resent until acknowledged. Receipts and the
// hello are fire-and-forget.
struct Payload {
    enum class Type : std::uint8_t {
        Text     = 1,
        Receipt  = 2,
        Reaction = 3,
        Hello    = 4,
    };
    enum class ReceiptKind : std::uint8_t {
        Delivered = 0,
        Read      = 1,
    };

    Type type = Type::Hello;

    // Text and Reaction: the id the receipt refers to
    MessageId mid {};

    // Text
    MessageId     reply_to {}; // all zero: not a reply
    std::uint64_t sent_at = 0; // milliseconds, as the sender's clock said
    std::string   text;

    // Receipt
    ReceiptKind            kind = ReceiptKind::Delivered;
    std::vector<MessageId> mids;

    // Reaction: target is the message reacted to; remove takes the reaction back
    MessageId   target {};
    bool        remove = false;
    std::string emoji;

    bool is_tracked() const { return type == Type::Text || type == Type::Reaction; }

    Bytes                                encode() const;
    static std::expected<Payload, Error> decode(std::span<const std::uint8_t> data);

    static Payload make_text(const MessageId &mid, std::string text, std::uint64_t sent_at, const MessageId &reply_to = {});
    static Payload make_receipt(ReceiptKind kind, std::vector<MessageId> mids);
    static Payload make_reaction(const MessageId &mid, const MessageId &target, std::string emoji, bool remove);
    static Payload make_hello();
};

// Largest text of one message in bytes and of one reaction
inline constexpr std::size_t MaxTextBytes     = 3900;
inline constexpr std::size_t MaxEmojiBytes    = 32;
inline constexpr std::size_t MaxReceiptMids   = 32;

} // namespace SecretChat
