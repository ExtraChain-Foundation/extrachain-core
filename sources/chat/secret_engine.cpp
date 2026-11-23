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

#include "chat/secret_engine.h"
#include "secret_util.h"

#include <sodium.h>

#include <algorithm>
#include <deque>
#include <map>
#include <set>

namespace SecretChat {

using namespace detail;

namespace {

constexpr std::size_t MaxConsumed = 256;
constexpr std::size_t MaxSeen     = 512;
constexpr std::uint64_t ReackGapMs = 5'000;

Key32 hash_of(std::span<const std::uint8_t> data) {
    Key32 out {};
    crypto_generichash(out.data(), out.size(), data.data(), data.size(), nullptr, 0);
    return out;
}

} // namespace

struct Engine::Impl {
    struct Outgoing {
        MessageId     mid {};
        MailboxId     id {};
        Bytes         wire;
        std::uint32_t attempts   = 0;
        std::uint64_t next_retry = 0;
        bool          failed     = false;
    };

    struct Chat {
        explicit Chat(Session s) : session(std::move(s)) {}
        Session                                      session;
        std::vector<Outgoing>                        out;
        std::deque<std::pair<MailboxId, MessageId>>  consumed;
        std::deque<MessageId>                        seen_order;
        std::set<MessageId>                          seen;
        Key32                                        initial_hash {};
        MessageId                                    first_mid {};
        std::map<MessageId, std::uint64_t>           reacked_at;
    };

    EngineConfig                  config;
    std::map<ChatId, Chat>        chats;

    explicit Impl(EngineConfig c) : config(std::move(c)) {}

    void emit(const Event &e) const {
        if (config.on_event)
            config.on_event(e);
    }

    void save(const ChatId &id, const Chat &chat) const {
        if (config.save_chat)
            config.save_chat(id, serialize(chat));
    }

    std::uint64_t pause_after(std::uint32_t attempts) const {
        const auto &steps = config.retry_ms;
        if (steps.empty())
            return 60'000;
        const std::size_t i = attempts == 0 ? 0 : std::min<std::size_t>(attempts - 1, steps.size() - 1);
        return steps[i];
    }

    // Encrypts one payload for a chat. The mailbox id has to be read before the message is made.
    std::expected<std::pair<MailboxId, Bytes>, Error> seal(Chat &chat, const Payload &p) {
        const auto id = chat.session.next_send_mailbox_id();
        if (!id.has_value())
            return std::unexpected(Error::NotReady);
        auto wire = chat.session.encrypt(p.encode());
        if (!wire.has_value())
            return std::unexpected(wire.error());
        return std::pair<MailboxId, Bytes> { *id, std::move(*wire) };
    }

    void transmit(const MailboxId &id, const Bytes &wire) const {
        if (config.transport)
            config.transport->send(id, wire);
    }

    // Fire and forget: receipts and hello
    void send_untracked(const ChatId &cid, Chat &chat, const Payload &p) {
        auto sealed = seal(chat, p);
        if (!sealed.has_value())
            return;
        transmit(sealed->first, sealed->second);
        save(cid, chat);
    }

    // Kept until the other side confirms it
    std::expected<MessageId, Error> send_tracked(const ChatId &cid, Chat &chat, const Payload &p, std::uint64_t now) {
        auto sealed = seal(chat, p);
        if (!sealed.has_value())
            return std::unexpected(sealed.error());
        Outgoing o;
        o.mid        = p.mid;
        o.id         = sealed->first;
        o.wire       = std::move(sealed->second);
        o.attempts   = 1;
        o.next_retry = now + pause_after(1);
        transmit(o.id, o.wire);
        chat.out.push_back(std::move(o));
        save(cid, chat);
        return p.mid;
    }

    void reack(const ChatId &cid, Chat &chat, const MessageId &mid, std::uint64_t now) {
        if (is_zero(mid))
            return;
        auto &last = chat.reacked_at[mid];
        if (last != 0 && now < last + ReackGapMs)
            return;
        last = now;
        send_untracked(cid, chat, Payload::make_receipt(Payload::ReceiptKind::Delivered, { mid }));
    }

    void remember_seen(Chat &chat, const MessageId &mid) {
        chat.seen.insert(mid);
        chat.seen_order.push_back(mid);
        while (chat.seen_order.size() > MaxSeen) {
            chat.seen.erase(chat.seen_order.front());
            chat.seen_order.pop_front();
        }
    }

    void remember_consumed(Chat &chat, const MailboxId &id, const MessageId &mid) {
        chat.consumed.emplace_back(id, mid);
        while (chat.consumed.size() > MaxConsumed)
            chat.consumed.pop_front();
    }

    void flush(Chat &chat, std::uint64_t now) {
        for (auto &o : chat.out) {
            transmit(o.id, o.wire);
            if (!o.failed) {
                ++o.attempts;
                o.next_retry = now + pause_after(o.attempts);
            }
        }
    }

    void handle_payload(const ChatId &cid, Chat &chat, const Payload &p, const MailboxId &id, std::uint64_t now) {
        switch (p.type) {
        case Payload::Type::Text: {
            remember_consumed(chat, id, p.mid);
            if (chat.seen.contains(p.mid)) {
                reack(cid, chat, p.mid, now);
                break;
            }
            remember_seen(chat, p.mid);
            Event e;
            e.kind     = Event::Kind::Message;
            e.chat     = cid;
            e.mid      = p.mid;
            e.reply_to = p.reply_to;
            e.text     = p.text;
            e.sent_at  = p.sent_at;
            emit(e);
            send_untracked(cid, chat, Payload::make_receipt(Payload::ReceiptKind::Delivered, { p.mid }));
            break;
        }
        case Payload::Type::Reaction: {
            remember_consumed(chat, id, p.mid);
            if (chat.seen.contains(p.mid)) {
                reack(cid, chat, p.mid, now);
                break;
            }
            remember_seen(chat, p.mid);
            Event e;
            e.kind    = Event::Kind::Reaction;
            e.chat    = cid;
            e.mid     = p.mid;
            e.target  = p.target;
            e.emoji   = p.emoji;
            e.removed = p.remove;
            emit(e);
            send_untracked(cid, chat, Payload::make_receipt(Payload::ReceiptKind::Delivered, { p.mid }));
            break;
        }
        case Payload::Type::Receipt: {
            remember_consumed(chat, id, MessageId {});
            for (const auto &mid : p.mids) {
                if (p.kind == Payload::ReceiptKind::Delivered) {
                    const auto it = std::find_if(chat.out.begin(), chat.out.end(),
                                                 [&](const Outgoing &o) { return o.mid == mid; });
                    if (it == chat.out.end())
                        continue;
                    chat.out.erase(it);
                }
                Event e;
                e.kind = p.kind == Payload::ReceiptKind::Delivered ? Event::Kind::Delivered : Event::Kind::Read;
                e.chat = cid;
                e.mid  = mid;
                emit(e);
            }
            break;
        }
        case Payload::Type::Hello: {
            remember_consumed(chat, id, MessageId {});
            Event e;
            e.kind = Event::Kind::PeerOnline;
            e.chat = cid;
            emit(e);
            flush(chat, now);
            break;
        }
        }
    }

    void handle_initial(const SignedPreKey &prekey, std::span<const std::uint8_t> wire, std::uint64_t now) {
        const Key32 digest = hash_of(wire);
        for (auto &[cid, chat] : chats) {
            if (chat.initial_hash == digest) {
                reack(cid, chat, chat.first_mid, now);
                return;
            }
        }
        auto accepted = Session::accept(config.identity, prekey, wire);
        if (!accepted.has_value())
            return;
        auto payload = Payload::decode(accepted->plaintext);
        if (!payload.has_value() || payload->type != Payload::Type::Text)
            return;

        const ChatId cid = accepted->session.peer_identity();
        chats.erase(cid);
        auto &chat        = chats.emplace(cid, Chat(std::move(accepted->session))).first->second;
        chat.initial_hash = digest;
        chat.first_mid    = payload->mid;
        remember_seen(chat, payload->mid);

        Event created;
        created.kind = Event::Kind::NewChat;
        created.chat = cid;
        emit(created);
        Event e;
        e.kind     = Event::Kind::Message;
        e.chat     = cid;
        e.mid      = payload->mid;
        e.reply_to = payload->reply_to;
        e.text     = payload->text;
        e.sent_at  = payload->sent_at;
        emit(e);
        send_untracked(cid, chat, Payload::make_receipt(Payload::ReceiptKind::Delivered, { payload->mid }));
    }

    static Bytes serialize(const Chat &chat) {
        Bytes out { 'S', 'C', 'C', 1 };
        put_bytes(out, chat.session.peer_identity());
        const Bytes session = chat.session.serialize();
        put_u32(out, static_cast<std::uint32_t>(session.size()));
        put_bytes(out, session);
        put_bytes(out, chat.initial_hash);
        put_bytes(out, chat.first_mid);
        put_u32(out, static_cast<std::uint32_t>(chat.out.size()));
        for (const auto &o : chat.out) {
            put_bytes(out, o.mid);
            put_bytes(out, o.id);
            put_u32(out, o.attempts);
            put_u64(out, o.next_retry);
            out.push_back(o.failed ? 1 : 0);
            put_u32(out, static_cast<std::uint32_t>(o.wire.size()));
            put_bytes(out, o.wire);
        }
        put_u32(out, static_cast<std::uint32_t>(chat.consumed.size()));
        for (const auto &[id, mid] : chat.consumed) {
            put_bytes(out, id);
            put_bytes(out, mid);
        }
        put_u32(out, static_cast<std::uint32_t>(chat.seen_order.size()));
        for (const auto &mid : chat.seen_order)
            put_bytes(out, mid);
        return out;
    }
};

Engine::Engine(EngineConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {
    (void)sodium_init();
}
Engine::~Engine() = default;

std::expected<ChatId, Error> Engine::start_chat(const PreKeyBundle &peer, std::string first_text, std::uint64_t now,
                                                MessageId *first_mid) {
    if (first_text.size() > MaxTextBytes || !utf8_valid(first_text))
        return std::unexpected(Error::InvalidArgument);
    const MessageId mid     = new_message_id();
    const Payload   payload = Payload::make_text(mid, std::move(first_text), now);
    auto            started = Session::initiate(impl_->config.identity, peer, payload.encode());
    if (!started.has_value())
        return std::unexpected(started.error());

    const ChatId cid = started->session.peer_identity();
    impl_->chats.erase(cid);
    auto &chat = impl_->chats.emplace(cid, Impl::Chat(std::move(started->session))).first->second;
    Impl::Outgoing o;
    o.mid        = mid;
    o.id         = started->mailbox_id;
    o.wire       = std::move(started->wire);
    o.attempts   = 1;
    o.next_retry = now + impl_->pause_after(1);
    impl_->transmit(o.id, o.wire);
    chat.out.push_back(std::move(o));
    impl_->save(cid, chat);
    if (first_mid)
        *first_mid = mid;
    return cid;
}

std::expected<MessageId, Error> Engine::send_text(const ChatId &chat, std::string text, std::uint64_t now,
                                                  const MessageId &reply_to) {
    auto it = impl_->chats.find(chat);
    if (it == impl_->chats.end())
        return std::unexpected(Error::UnknownChat);
    if (text.size() > MaxTextBytes || !utf8_valid(text))
        return std::unexpected(Error::InvalidArgument);
    return impl_->send_tracked(chat, it->second, Payload::make_text(new_message_id(), std::move(text), now, reply_to), now);
}

std::expected<MessageId, Error> Engine::send_reaction(const ChatId &chat, const MessageId &target, std::string emoji,
                                                      bool remove, std::uint64_t now) {
    auto it = impl_->chats.find(chat);
    if (it == impl_->chats.end())
        return std::unexpected(Error::UnknownChat);
    if (is_zero(target))
        return std::unexpected(Error::InvalidArgument);
    Payload p = Payload::make_reaction(new_message_id(), target, std::move(emoji), remove);
    // The same checks the receiving side makes, so nothing is queued that the other side would refuse
    if (!Payload::decode(p.encode()).has_value())
        return std::unexpected(Error::InvalidArgument);
    return impl_->send_tracked(chat, it->second, p, now);
}

std::expected<void, Error> Engine::send_read(const ChatId &chat, std::vector<MessageId> mids, std::uint64_t) {
    auto it = impl_->chats.find(chat);
    if (it == impl_->chats.end())
        return std::unexpected(Error::UnknownChat);
    if (mids.empty())
        return {};
    // A receipt names a bounded number of messages; longer lists go in several
    for (std::size_t i = 0; i < mids.size(); i += MaxReceiptMids) {
        const std::size_t end = std::min(mids.size(), i + MaxReceiptMids);
        impl_->send_untracked(chat, it->second,
                              Payload::make_receipt(Payload::ReceiptKind::Read,
                                                    std::vector<MessageId>(mids.begin() + static_cast<std::ptrdiff_t>(i),
                                                                           mids.begin() + static_cast<std::ptrdiff_t>(end))));
    }
    return {};
}

void Engine::on_envelope(const MailboxId &id, std::span<const std::uint8_t> wire, std::uint64_t now) {
    for (const auto &prekey : impl_->config.prekeys) {
        if (id == initial_mailbox_id(prekey.public_key)) {
            impl_->handle_initial(prekey, wire, now);
            return;
        }
    }
    for (auto &[cid, chat] : impl_->chats) {
        const auto seen = std::find_if(chat.consumed.begin(), chat.consumed.end(),
                                       [&](const auto &entry) { return entry.first == id; });
        if (seen != chat.consumed.end()) {
            // The same message again: its acknowledgement may have been lost
            impl_->reack(cid, chat, seen->second, now);
            return;
        }
        const auto expected = chat.session.expected_receive_mailbox_ids();
        if (std::find(expected.begin(), expected.end(), id) == expected.end())
            continue;
        auto plain = chat.session.decrypt(wire);
        if (!plain.has_value())
            return;
        auto payload = Payload::decode(*plain);
        if (!payload.has_value())
            return;
        ChatId key = cid;
        impl_->handle_payload(key, chat, *payload, id, now);
        impl_->save(key, chat);
        return;
    }
}

void Engine::on_connected(std::uint64_t now) {
    for (auto &[cid, chat] : impl_->chats) {
        if (impl_->config.announce_presence)
            impl_->send_untracked(cid, chat, Payload::make_hello());
        impl_->flush(chat, now);
        impl_->save(cid, chat);
    }
}

void Engine::tick(std::uint64_t now) {
    for (auto &[cid, chat] : impl_->chats) {
        bool changed = false;
        for (auto &o : chat.out) {
            if (now < o.next_retry)
                continue;
            if (o.failed) {
                impl_->transmit(o.id, o.wire);
                o.next_retry = now + impl_->config.failed_retry_ms;
                changed      = true;
                continue;
            }
            if (o.attempts >= impl_->config.max_attempts) {
                o.failed     = true;
                o.next_retry = now + impl_->config.failed_retry_ms;
                changed      = true;
                Event e;
                e.kind = Event::Kind::Failed;
                e.chat = cid;
                e.mid  = o.mid;
                impl_->emit(e);
                continue;
            }
            impl_->transmit(o.id, o.wire);
            ++o.attempts;
            o.next_retry = now + impl_->pause_after(o.attempts);
            changed      = true;
        }
        if (changed)
            impl_->save(cid, chat);
    }
}

std::expected<void, Error> Engine::restore_chat(std::span<const std::uint8_t> blob) {
    Reader                      r(blob);
    std::array<std::uint8_t, 4> magic {};
    if (!r.take(magic) || magic != std::array<std::uint8_t, 4> { 'S', 'C', 'C', 1 })
        return std::unexpected(Error::BadFormat);
    ChatId        peer {};
    std::uint32_t session_len = 0;
    if (!r.take(peer) || !r.u32(session_len) || session_len > r.remaining())
        return std::unexpected(Error::BadFormat);
    Bytes session_bytes(session_len);
    if (session_len != 0 && !r.take(session_bytes.data(), session_len))
        return std::unexpected(Error::BadFormat);
    auto session = Session::deserialize(session_bytes);
    if (!session.has_value())
        return std::unexpected(session.error());

    Impl::Chat chat(std::move(*session));
    std::uint32_t count = 0;
    if (!r.take(chat.initial_hash) || !r.take(chat.first_mid) || !r.u32(count) || count > 100000)
        return std::unexpected(Error::BadFormat);
    for (std::uint32_t i = 0; i < count; ++i) {
        Impl::Outgoing o;
        std::uint8_t   failed   = 0;
        std::uint32_t  wire_len = 0;
        if (!r.take(o.mid) || !r.take(o.id) || !r.u32(o.attempts) || !r.u64(o.next_retry) || !r.u8(failed)
            || failed > 1 || !r.u32(wire_len) || wire_len > r.remaining())
            return std::unexpected(Error::BadFormat);
        o.failed = failed == 1;
        o.wire.resize(wire_len);
        if (wire_len != 0 && !r.take(o.wire.data(), wire_len))
            return std::unexpected(Error::BadFormat);
        chat.out.push_back(std::move(o));
    }
    if (!r.u32(count) || count > MaxConsumed)
        return std::unexpected(Error::BadFormat);
    for (std::uint32_t i = 0; i < count; ++i) {
        MailboxId id {};
        MessageId mid {};
        if (!r.take(id) || !r.take(mid))
            return std::unexpected(Error::BadFormat);
        chat.consumed.emplace_back(id, mid);
    }
    if (!r.u32(count) || count > MaxSeen)
        return std::unexpected(Error::BadFormat);
    for (std::uint32_t i = 0; i < count; ++i) {
        MessageId mid {};
        if (!r.take(mid))
            return std::unexpected(Error::BadFormat);
        chat.seen.insert(mid);
        chat.seen_order.push_back(mid);
    }
    if (r.remaining() != 0)
        return std::unexpected(Error::BadFormat);
    impl_->chats.erase(peer);
    impl_->chats.emplace(peer, std::move(chat));
    return {};
}

void Engine::remove_chat(const ChatId &chat) {
    impl_->chats.erase(chat);
}

std::vector<ChatId> Engine::chats() const {
    std::vector<ChatId> out;
    for (const auto &[cid, chat] : impl_->chats)
        out.push_back(cid);
    return out;
}

std::optional<std::string> Engine::safety_number(const ChatId &chat) const {
    const auto it = impl_->chats.find(chat);
    if (it == impl_->chats.end())
        return std::nullopt;
    return it->second.session.safety_number();
}

std::vector<Engine::Pending> Engine::pending(const ChatId &chat) const {
    std::vector<Pending> out;
    const auto           it = impl_->chats.find(chat);
    if (it == impl_->chats.end())
        return out;
    for (const auto &o : it->second.out)
        out.push_back({ o.mid, o.attempts, o.failed });
    return out;
}

} // namespace SecretChat
