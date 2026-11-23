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

#include "chat/secret_payload.h"
#include "secret_util.h"

#include <sodium.h>

#include <algorithm>

namespace SecretChat {

using namespace detail;

namespace {
constexpr std::uint8_t PayloadVersion = 1;
}

MessageId new_message_id() {
    MessageId id {};
    do {
        randombytes_buf(id.data(), id.size());
    } while (is_zero(id));
    return id;
}

bool is_zero(const MessageId &id) {
    return std::all_of(id.begin(), id.end(), [](std::uint8_t b) { return b == 0; });
}

Bytes Payload::encode() const {
    Bytes out { PayloadVersion, static_cast<std::uint8_t>(type) };
    switch (type) {
    case Type::Text:
        put_bytes(out, mid);
        put_bytes(out, reply_to);
        put_u64(out, sent_at);
        out.insert(out.end(), text.begin(), text.end());
        break;
    case Type::Receipt:
        out.push_back(static_cast<std::uint8_t>(kind));
        out.push_back(static_cast<std::uint8_t>(mids.size()));
        for (const auto &m : mids)
            put_bytes(out, m);
        break;
    case Type::Reaction:
        put_bytes(out, mid);
        put_bytes(out, target);
        out.push_back(remove ? 1 : 0);
        out.insert(out.end(), emoji.begin(), emoji.end());
        break;
    case Type::Hello:
        break;
    }
    return out;
}

std::expected<Payload, Error> Payload::decode(std::span<const std::uint8_t> data) {
    Reader       r(data);
    std::uint8_t version = 0;
    std::uint8_t type    = 0;
    if (!r.u8(version) || version != PayloadVersion || !r.u8(type))
        return std::unexpected(Error::BadFormat);
    Payload p;
    switch (type) {
    case static_cast<std::uint8_t>(Type::Text): {
        p.type = Type::Text;
        if (!r.take(p.mid) || is_zero(p.mid) || !r.take(p.reply_to) || !r.u64(p.sent_at))
            return std::unexpected(Error::BadFormat);
        const auto rest = r.rest();
        if (rest.size() > MaxTextBytes)
            return std::unexpected(Error::BadFormat);
        p.text.assign(rest.begin(), rest.end());
        if (!utf8_valid(p.text))
            return std::unexpected(Error::BadFormat);
        break;
    }
    case static_cast<std::uint8_t>(Type::Receipt): {
        p.type = Type::Receipt;
        std::uint8_t kind  = 0;
        std::uint8_t count = 0;
        if (!r.u8(kind) || kind > 1 || !r.u8(count) || count == 0 || count > MaxReceiptMids)
            return std::unexpected(Error::BadFormat);
        p.kind = static_cast<ReceiptKind>(kind);
        for (std::uint8_t i = 0; i < count; ++i) {
            MessageId m {};
            if (!r.take(m))
                return std::unexpected(Error::BadFormat);
            p.mids.push_back(m);
        }
        if (r.remaining() != 0)
            return std::unexpected(Error::BadFormat);
        break;
    }
    case static_cast<std::uint8_t>(Type::Reaction): {
        p.type = Type::Reaction;
        std::uint8_t flags = 0;
        if (!r.take(p.mid) || is_zero(p.mid) || !r.take(p.target) || is_zero(p.target) || !r.u8(flags) || flags > 1)
            return std::unexpected(Error::BadFormat);
        p.remove = flags == 1;
        const auto rest = r.rest();
        if (rest.size() > MaxEmojiBytes)
            return std::unexpected(Error::BadFormat);
        p.emoji.assign(rest.begin(), rest.end());
        if (!utf8_valid(p.emoji) || (!p.remove && p.emoji.empty()))
            return std::unexpected(Error::BadFormat);
        for (const char ch : p.emoji) {
            if (static_cast<std::uint8_t>(ch) < 0x20 || static_cast<std::uint8_t>(ch) == 0x7F)
                return std::unexpected(Error::BadFormat);
        }
        break;
    }
    case static_cast<std::uint8_t>(Type::Hello):
        p.type = Type::Hello;
        if (r.remaining() != 0)
            return std::unexpected(Error::BadFormat);
        break;
    default:
        return std::unexpected(Error::BadFormat);
    }
    return p;
}

Payload Payload::make_text(const MessageId &mid, std::string text, std::uint64_t sent_at, const MessageId &reply_to) {
    Payload p;
    p.type     = Type::Text;
    p.mid      = mid;
    p.reply_to = reply_to;
    p.sent_at  = sent_at;
    p.text     = std::move(text);
    return p;
}

Payload Payload::make_receipt(ReceiptKind kind, std::vector<MessageId> mids) {
    Payload p;
    p.type = Type::Receipt;
    p.kind = kind;
    p.mids = std::move(mids);
    return p;
}

Payload Payload::make_reaction(const MessageId &mid, const MessageId &target, std::string emoji, bool remove) {
    Payload p;
    p.type   = Type::Reaction;
    p.mid    = mid;
    p.target = target;
    p.emoji  = std::move(emoji);
    p.remove = remove;
    return p;
}

Payload Payload::make_hello() {
    Payload p;
    p.type = Type::Hello;
    return p;
}

} // namespace SecretChat
