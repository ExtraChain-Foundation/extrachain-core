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

#include <sodium.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>

using namespace SecretChat;

namespace {

int failures = 0;
int checks   = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        ++checks;                                                        \
        if (!(cond)) {                                                   \
            ++failures;                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);  \
        }                                                                \
    } while (0)

struct Net;

struct Node : Transport {
    std::string                 name;
    Net                        *net = nullptr;
    std::unique_ptr<Engine>     engine;
    std::vector<Event>          events;
    std::map<ChatId, Bytes>     saved;
    bool                        online = true;
    IdentityKeys                identity;
    SignedPreKey                prekey;

    void send(const MailboxId &id, const Bytes &wire) override;

    int count(Event::Kind k) const {
        return static_cast<int>(std::count_if(events.begin(), events.end(), [&](const Event &e) { return e.kind == k; }));
    }
    std::vector<Event> of(Event::Kind k) const {
        std::vector<Event> out;
        for (const auto &e : events)
            if (e.kind == k)
                out.push_back(e);
        return out;
    }
    void clear() { events.clear(); }
};

struct Net {
    struct Packet {
        Node     *from;
        Node     *to;
        MailboxId id;
        Bytes     wire;
    };
    Node                                a, b;
    std::vector<Packet>                 queue;
    std::function<bool(const Packet &)> drop;
    bool                                duplicate = false;
    std::uint64_t                       rng       = 88172645463325252ull;
    std::uint64_t                       now       = 1'000'000;

    void clear() {
        a.clear();
        b.clear();
    }

    std::uint64_t next() {
        rng ^= rng << 13;
        rng ^= rng >> 7;
        rng ^= rng << 17;
        return rng;
    }

    void push(Node *from, const MailboxId &id, const Bytes &wire) {
        Node *to = from == &a ? &b : &a;
        Packet p { from, to, id, wire };
        if (!to->online || (drop && drop(p)))
            return;
        queue.push_back(p);
        if (duplicate)
            queue.push_back(p);
    }

    void pump() {
        for (int guard = 0; !queue.empty() && guard < 100000; ++guard) {
            Packet p = queue.front();
            queue.erase(queue.begin());
            if (p.to->online)
                p.to->engine->on_envelope(p.id, p.wire, now);
        }
    }

    void make(Node &n, const std::string &name, std::uint8_t seed_byte, bool fresh_prekey = true) {
        n.name = name;
        n.net  = this;
        std::array<std::uint8_t, 32> seed {};
        seed.fill(seed_byte);
        n.identity = IdentityKeys::from_seed(seed).value();
        if (fresh_prekey)
            n.prekey = SignedPreKey::generate(n.identity, 1).value();
        revive(n);
    }

    void revive(Node &n, EngineConfig extra = {}) {
        EngineConfig c      = std::move(extra);
        c.identity          = n.identity;
        c.prekeys           = { n.prekey };
        c.transport         = &n;
        c.on_event          = [&n](const Event &e) { n.events.push_back(e); };
        c.save_chat         = [&n](const ChatId &id, const Bytes &blob) { n.saved[id] = blob; };
        n.engine            = std::make_unique<Engine>(std::move(c));
    }
};

void test_basic_flow() {
    Net net;
    net.make(net.a, "alice", 1);
    net.make(net.b, "bob", 2);

    MessageId first {};
    auto      chat = net.a.engine->start_chat(net.b.prekey.bundle, "hi bob", net.now, &first);
    CHECK(chat.has_value());
    net.pump();

    CHECK(net.b.events.size() >= 2);
    CHECK(net.b.events[0].kind == Event::Kind::NewChat);
    CHECK(net.b.events[1].kind == Event::Kind::Message && net.b.events[1].text == "hi bob");
    CHECK(net.b.events[1].mid == first);
    CHECK(net.a.count(Event::Kind::Delivered) == 1 && net.a.of(Event::Kind::Delivered)[0].mid == first);
    CHECK(net.a.engine->pending(*chat).empty());
    CHECK(net.a.engine->safety_number(*chat) == net.b.engine->safety_number(net.b.engine->chats().at(0)));

    const ChatId bob_side = net.b.engine->chats().at(0);
    auto reply = net.b.engine->send_text(bob_side, "hi alice", net.now);
    CHECK(reply.has_value());
    net.pump();
    CHECK(net.a.count(Event::Kind::Message) == 1 && net.a.of(Event::Kind::Message)[0].text == "hi alice");
    CHECK(net.b.count(Event::Kind::Delivered) == 1);

    // A reaction is delivered and acknowledged like a message
    auto reaction = net.a.engine->send_reaction(*chat, *reply, "\xF0\x9F\x91\x8D", false, net.now);
    CHECK(reaction.has_value());
    net.pump();
    auto reactions = net.b.of(Event::Kind::Reaction);
    CHECK(reactions.size() == 1 && reactions[0].target == *reply && reactions[0].emoji == "\xF0\x9F\x91\x8D"
          && !reactions[0].removed);
    CHECK(net.a.count(Event::Kind::Delivered) == 2);
    // and taken back
    CHECK(net.a.engine->send_reaction(*chat, *reply, "", true, net.now).has_value());
    net.pump();
    reactions = net.b.of(Event::Kind::Reaction);
    CHECK(reactions.size() == 2 && reactions[1].removed);

    // Read receipt: two ticks on the other side
    CHECK(net.a.engine->send_read(*chat, { *reply }, net.now).has_value());
    net.pump();
    CHECK(net.b.count(Event::Kind::Read) == 1 && net.b.of(Event::Kind::Read)[0].mid == *reply);
    CHECK(net.b.engine->pending(bob_side).empty());
}

void test_lost_first_message_is_resent() {
    Net net;
    net.make(net.a, "alice", 1);
    net.make(net.b, "bob", 2);
    int dropped = 0;
    net.drop = [&](const Net::Packet &p) { return p.from == &net.a && dropped++ == 0; };
    auto chat = net.a.engine->start_chat(net.b.prekey.bundle, "first", net.now);
    CHECK(chat.has_value());
    net.pump();
    CHECK(net.b.events.empty());
    net.now += 9'000;
    net.a.engine->tick(net.now);
    net.pump();
    CHECK(net.b.events.empty()); // not yet due
    net.now += 2'000;
    net.a.engine->tick(net.now);
    net.pump();
    CHECK(net.b.count(Event::Kind::Message) == 1);
    CHECK(net.a.count(Event::Kind::Delivered) == 1);
}

void test_lost_ack_does_not_duplicate() {
    Net net;
    net.make(net.a, "alice", 1);
    net.make(net.b, "bob", 2);
    auto chat = net.a.engine->start_chat(net.b.prekey.bundle, "once", net.now);
    net.pump();
    const ChatId bob_side = net.b.engine->chats().at(0);
    CHECK(net.b.engine->send_text(bob_side, "warm up", net.now).has_value());
    net.pump();
    net.clear();

    // Bob's acknowledgements are lost for a while
    bool lose_acks = true;
    net.drop       = [&](const Net::Packet &p) { return lose_acks && p.from == &net.b; };
    auto mid = net.a.engine->send_text(*chat, "did you get this", net.now).value();
    net.pump();
    CHECK(net.b.count(Event::Kind::Message) == 1);
    CHECK(net.a.count(Event::Kind::Delivered) == 0);

    lose_acks = false;
    net.now += 11'000;
    net.a.engine->tick(net.now); // resends the very same bytes
    net.pump();
    CHECK(net.b.count(Event::Kind::Message) == 1); // not shown twice
    CHECK(net.a.count(Event::Kind::Delivered) == 1 && net.a.of(Event::Kind::Delivered)[0].mid == mid);
    CHECK(net.a.engine->pending(*chat).empty());
}

void test_duplicated_network() {
    Net net;
    net.duplicate = true;
    net.make(net.a, "alice", 1);
    net.make(net.b, "bob", 2);
    auto chat = net.a.engine->start_chat(net.b.prekey.bundle, "dup?", net.now);
    net.pump();
    CHECK(net.b.engine->chats().size() == 1);
    CHECK(net.b.count(Event::Kind::NewChat) == 1);
    CHECK(net.b.count(Event::Kind::Message) == 1);
    const ChatId bob_side = net.b.engine->chats().at(0);
    for (int i = 0; i < 5; ++i)
        CHECK(net.a.engine->send_text(*chat, "m" + std::to_string(i), net.now).has_value());
    net.pump();
    CHECK(net.b.count(Event::Kind::Message) == 6);
    CHECK(net.a.count(Event::Kind::Delivered) == 6);
    (void)bob_side;
}

void test_offline_recipient_gets_it_after_hello() {
    Net net;
    net.make(net.a, "alice", 1);
    net.make(net.b, "bob", 2);
    auto chat = net.a.engine->start_chat(net.b.prekey.bundle, "hello", net.now);
    net.pump();
    const ChatId bob_side = net.b.engine->chats().at(0);
    CHECK(net.b.engine->send_text(bob_side, "hello back", net.now).has_value());
    net.pump();
    net.clear();

    net.b.online = false;
    std::vector<MessageId> sent;
    for (int i = 0; i < 3; ++i)
        sent.push_back(net.a.engine->send_text(*chat, "while away " + std::to_string(i), net.now).value());
    net.pump();
    CHECK(net.b.events.empty());

    // Time passes with no one there: attempts run out, the messages stay queued as failed
    for (int step = 0; step < 4000; ++step) {
        net.now += 1'000;
        net.a.engine->tick(net.now);
        net.pump();
    }
    CHECK(net.a.count(Event::Kind::Failed) == 3);
    auto pending = net.a.engine->pending(*chat);
    CHECK(pending.size() == 3);
    for (const auto &p : pending)
        CHECK(p.failed);

    // Bob comes back and says hello: Alice sends everything at once
    net.b.online = true;
    net.b.engine->on_connected(net.now);
    net.pump();
    CHECK(net.a.count(Event::Kind::PeerOnline) == 1);
    auto got = net.b.of(Event::Kind::Message);
    CHECK(got.size() == 3);
    for (std::size_t i = 0; i < got.size() && i < 3; ++i)
        CHECK(got[i].text == "while away " + std::to_string(i));
    CHECK(net.a.count(Event::Kind::Delivered) == 3);
    CHECK(net.a.engine->pending(*chat).empty());
}

void test_presence_can_be_switched_off() {
    Net net;
    net.make(net.a, "alice", 1);
    EngineConfig quiet;
    quiet.announce_presence = false;
    net.make(net.b, "bob", 2);
    net.revive(net.b, std::move(quiet));
    net.a.engine->start_chat(net.b.prekey.bundle, "x", net.now);
    net.pump();
    net.clear();
    net.b.engine->on_connected(net.now);
    net.pump();
    CHECK(net.a.count(Event::Kind::PeerOnline) == 0);
}

// With presence switched off nobody tells Alice that Bob is back: a message that ran out of
// attempts must still be tried again now and then.
void test_failed_messages_keep_being_tried() {
    Net net;
    net.make(net.a, "alice", 1);
    EngineConfig quiet;
    quiet.announce_presence = false;
    net.make(net.b, "bob", 2);
    net.revive(net.b, std::move(quiet));
    auto chat = net.a.engine->start_chat(net.b.prekey.bundle, "hello", net.now);
    net.pump();
    net.clear();
    net.b.online = false;
    auto mid = net.a.engine->send_text(*chat, "are you there", net.now).value();
    for (int step = 0; step < 3000; ++step) {
        net.now += 1'000;
        net.a.engine->tick(net.now);
        net.pump();
    }
    CHECK(net.a.count(Event::Kind::Failed) == 1);
    CHECK(net.a.engine->pending(*chat).size() == 1 && net.a.engine->pending(*chat)[0].failed);

    net.b.online = true; // no hello: nothing tells Alice
    for (int step = 0; step < 1200; ++step) {
        net.now += 1'000;
        net.a.engine->tick(net.now);
        net.pump();
    }
    CHECK(net.b.count(Event::Kind::Message) == 1 && net.b.of(Event::Kind::Message)[0].mid == mid);
    CHECK(net.a.count(Event::Kind::Delivered) == 1);
    CHECK(net.a.engine->pending(*chat).empty());
    CHECK(net.a.count(Event::Kind::Failed) == 1); // reported once, not on every retry
}

void test_restart_keeps_the_conversation_and_the_queue() {
    Net net;
    net.make(net.a, "alice", 1);
    net.make(net.b, "bob", 2);
    auto chat = net.a.engine->start_chat(net.b.prekey.bundle, "before restart", net.now);
    net.pump();
    const ChatId bob_side = net.b.engine->chats().at(0);
    CHECK(net.b.engine->send_text(bob_side, "ready", net.now).has_value());
    net.pump();

    // Bob is away while Alice sends; then Alice's app restarts from what it saved
    net.clear();
    net.b.online = false;
    auto pending_mid = net.a.engine->send_text(*chat, "queued across a restart", net.now).value();
    net.pump();
    const Bytes blob = net.a.saved.at(*chat);
    net.revive(net.a);
    CHECK(net.a.engine->restore_chat(blob).has_value());
    CHECK(net.a.engine->chats().size() == 1);
    CHECK(net.a.engine->pending(*chat).size() == 1);

    net.b.online = true;
    net.b.engine->on_connected(net.now);
    net.pump();
    auto got = net.b.of(Event::Kind::Message);
    CHECK(!got.empty() && got.back().text == "queued across a restart" && got.back().mid == pending_mid);
    CHECK(net.a.count(Event::Kind::Delivered) == 1);

    // The restored session keeps working in both directions
    CHECK(net.a.engine->send_text(*chat, "after restart", net.now).has_value());
    CHECK(net.b.engine->send_text(bob_side, "welcome back", net.now).has_value());
    net.pump();
    CHECK(net.b.of(Event::Kind::Message).back().text == "after restart");
    CHECK(net.a.of(Event::Kind::Message).back().text == "welcome back");

    CHECK(!net.a.engine->restore_chat(Bytes { 1, 2, 3 }).has_value());
    Bytes damaged = blob;
    damaged.resize(damaged.size() - 3);
    CHECK(!net.a.engine->restore_chat(damaged).has_value());
}

void test_garbage_is_ignored() {
    Net net;
    net.make(net.a, "alice", 1);
    net.make(net.b, "bob", 2);
    auto chat = net.a.engine->start_chat(net.b.prekey.bundle, "hello", net.now);
    net.pump();
    const ChatId bob_side = net.b.engine->chats().at(0);
    net.clear();

    MailboxId random_id {};
    randombytes_buf(random_id.data(), random_id.size());
    Bytes garbage(300);
    randombytes_buf(garbage.data(), garbage.size());
    net.b.engine->on_envelope(random_id, garbage, net.now);
    net.b.engine->on_envelope(initial_mailbox_id(net.b.prekey.public_key), garbage, net.now);
    // A real expected id with damaged contents
    auto id = net.a.engine->send_text(*chat, "real", net.now);
    CHECK(id.has_value());
    CHECK(net.b.events.empty());
    net.pump();
    CHECK(net.b.count(Event::Kind::Message) == 1);
    // The conversation is untouched
    CHECK(net.b.engine->send_text(bob_side, "still fine", net.now).has_value());
    net.pump();
    CHECK(net.a.count(Event::Kind::Message) == 1);
}

void test_arguments_are_checked() {
    Net net;
    net.make(net.a, "alice", 1);
    net.make(net.b, "bob", 2);
    auto chat = net.a.engine->start_chat(net.b.prekey.bundle, "hello", net.now);
    net.pump();
    ChatId nobody {};
    CHECK(net.a.engine->send_text(nobody, "x", net.now).error() == Error::UnknownChat);
    CHECK(net.a.engine->send_text(*chat, std::string("\xFF\xFE"), net.now).error() == Error::InvalidArgument);
    CHECK(net.a.engine->send_text(*chat, std::string(MaxTextBytes + 1, 'a'), net.now).error() == Error::InvalidArgument);
    CHECK(net.a.engine->send_text(*chat, std::string(MaxTextBytes, 'a'), net.now).has_value());
    CHECK(net.a.engine->send_reaction(*chat, MessageId {}, "x", false, net.now).error() == Error::InvalidArgument);
    CHECK(net.a.engine->send_reaction(*chat, new_message_id(), "", false, net.now).error() == Error::InvalidArgument);
    CHECK(net.a.engine->send_reaction(*chat, new_message_id(), std::string(40, 'x'), false, net.now).error()
          == Error::InvalidArgument);
    CHECK(net.a.engine->send_reaction(*chat, new_message_id(), "\n", false, net.now).error() == Error::InvalidArgument);
    CHECK(!net.a.engine->start_chat(net.b.prekey.bundle, std::string("\xC0\x80"), net.now).has_value());
    auto forged = net.b.prekey.bundle;
    forged.signature[0] ^= 1;
    CHECK(net.a.engine->start_chat(forged, "hi", net.now).error() == Error::BadSignature);
    CHECK(net.a.engine->send_read(nobody, { new_message_id() }, net.now).error() == Error::UnknownChat);
    // A long list of read messages goes in several receipts
    std::vector<MessageId> many;
    for (int i = 0; i < 70; ++i)
        many.push_back(new_message_id());
    CHECK(net.a.engine->send_read(*chat, many, net.now).has_value());
    net.pump();
    CHECK(net.b.count(Event::Kind::Read) == 70);
}

void test_peer_that_reinstalls_replaces_the_chat() {
    Net net;
    net.make(net.a, "alice", 1);
    net.make(net.b, "bob", 2);
    auto chat = net.a.engine->start_chat(net.b.prekey.bundle, "first life", net.now);
    net.pump();
    net.clear();
    // Alice loses her state and starts again with the same identity
    net.revive(net.a);
    auto again = net.a.engine->start_chat(net.b.prekey.bundle, "second life", net.now);
    CHECK(again.has_value() && *again == *chat);
    net.pump();
    CHECK(net.b.count(Event::Kind::NewChat) == 1);
    CHECK(net.b.of(Event::Kind::Message).back().text == "second life");
    CHECK(net.b.engine->chats().size() == 1);
    CHECK(net.b.engine->send_text(net.b.engine->chats().at(0), "welcome again", net.now).has_value());
    net.pump();
    CHECK(net.a.of(Event::Kind::Message).back().text == "welcome again");
}

// Both sides talk over a lossy network that reorders and drops a third of everything.
// Every message must arrive exactly once and every sender must end up with an empty queue.
void test_lossy_conversation() {
    for (std::uint64_t seed = 1; seed <= 5; ++seed) {
        Net net;
        net.rng = 0x9E3779B97F4A7C15ull * seed;
        net.make(net.a, "alice", 1);
        net.make(net.b, "bob", 2);
        net.drop = [&](const Net::Packet &) { return net.next() % 100 < 30; };

        std::set<MessageId> sent_by_a;
        std::set<MessageId> sent_by_b;
        MessageId           first {};
        auto                chat = net.a.engine->start_chat(net.b.prekey.bundle, "start", net.now, &first);
        sent_by_a.insert(first);
        ChatId bob_side {};
        int    sent = 0;

        for (int step = 0; step < 6000; ++step) {
            net.now += 1'000;
            if (bob_side == ChatId {} && !net.b.engine->chats().empty())
                bob_side = net.b.engine->chats().at(0);
            if (step < 300 && step % 10 == 0 && sent < 30) {
                sent_by_a.insert(net.a.engine->send_text(*chat, "a" + std::to_string(sent), net.now).value());
                if (bob_side != ChatId {})
                    sent_by_b.insert(net.b.engine->send_text(bob_side, "b" + std::to_string(sent), net.now).value());
                ++sent;
            }
            net.a.engine->tick(net.now);
            net.b.engine->tick(net.now);
            // Reorder what is in flight
            for (std::size_t i = net.queue.size(); i > 1; --i)
                std::swap(net.queue[i - 1], net.queue[net.next() % i]);
            net.pump();
            if (step > 300 && net.a.engine->pending(*chat).empty() && net.b.engine->pending(bob_side).empty())
                break;
        }

        // Receipts of the tail may still be on their way; give the sides a final nudge
        net.a.engine->on_connected(net.now);
        net.b.engine->on_connected(net.now);
        for (int i = 0; i < 20; ++i) {
            net.now += 1'000;
            net.pump();
        }

        CHECK(net.a.engine->pending(*chat).empty());
        CHECK(net.b.engine->pending(bob_side).empty());
        std::multiset<std::string> at_b;
        std::multiset<std::string> at_a;
        for (const auto &e : net.b.of(Event::Kind::Message))
            at_b.insert(e.text);
        for (const auto &e : net.a.of(Event::Kind::Message))
            at_a.insert(e.text);
        CHECK(at_b.size() == 31);
        CHECK(at_a.size() == sent_by_b.size());
        CHECK(std::set<std::string>(at_b.begin(), at_b.end()).size() == at_b.size()); // none twice
        CHECK(std::set<std::string>(at_a.begin(), at_a.end()).size() == at_a.size());
        std::set<MessageId> delivered_a;
        for (const auto &e : net.a.of(Event::Kind::Delivered))
            delivered_a.insert(e.mid);
        CHECK(delivered_a == sent_by_a);
        std::set<MessageId> delivered_b;
        for (const auto &e : net.b.of(Event::Kind::Delivered))
            delivered_b.insert(e.mid);
        CHECK(delivered_b == sent_by_b);
        CHECK(sent_by_b.size() >= 25);
    }
}

void test_payload_codec() {
    const MessageId m1 = new_message_id();
    const MessageId m2 = new_message_id();
    auto text = Payload::decode(Payload::make_text(m1, "h\xC3\xA9llo \xF0\x9F\x98\x80", 12345, m2).encode());
    CHECK(text.has_value() && text->mid == m1 && text->reply_to == m2 && text->sent_at == 12345
          && text->text == "h\xC3\xA9llo \xF0\x9F\x98\x80");
    auto receipt = Payload::decode(Payload::make_receipt(Payload::ReceiptKind::Read, { m1, m2 }).encode());
    CHECK(receipt.has_value() && receipt->kind == Payload::ReceiptKind::Read && receipt->mids.size() == 2);
    auto reaction = Payload::decode(Payload::make_reaction(m1, m2, "\xE2\x9D\xA4", false).encode());
    CHECK(reaction.has_value() && reaction->target == m2 && reaction->emoji == "\xE2\x9D\xA4" && !reaction->remove);
    CHECK(Payload::decode(Payload::make_hello().encode()).has_value());

    CHECK(!Payload::decode(Bytes {}).has_value());
    CHECK(!Payload::decode(Bytes { 2, 4 }).has_value());      // unknown version
    CHECK(!Payload::decode(Bytes { 1, 9 }).has_value());      // unknown type
    CHECK(!Payload::decode(Bytes { 1, 4, 0 }).has_value());   // trailing bytes on a hello
    Bytes zero_mid = Payload::make_text(MessageId {}, "x", 1).encode();
    CHECK(!Payload::decode(zero_mid).has_value());
    Bytes bad_utf8 = Payload::make_text(m1, "x", 1).encode();
    bad_utf8.push_back(0xFF);
    CHECK(!Payload::decode(bad_utf8).has_value());
    Bytes truncated_surrogate = Payload::make_text(m1, "", 1).encode();
    for (std::uint8_t b : { 0xED, 0xA0, 0x80 }) // a UTF-16 surrogate encoded in UTF-8
        truncated_surrogate.push_back(b);
    CHECK(!Payload::decode(truncated_surrogate).has_value());
    CHECK(!Payload::decode(Payload::make_text(m1, std::string(MaxTextBytes + 1, 'a'), 1).encode()).has_value());
    CHECK(!Payload::decode(Payload::make_receipt(Payload::ReceiptKind::Read, {}).encode()).has_value());
    CHECK(!Payload::decode(Payload::make_receipt(Payload::ReceiptKind::Read, std::vector<MessageId>(33, m1)).encode())
               .has_value());
    CHECK(!Payload::decode(Payload::make_reaction(m1, m2, "", false).encode()).has_value());
    CHECK(Payload::decode(Payload::make_reaction(m1, m2, "", true).encode()).has_value());
    CHECK(!Payload::decode(Payload::make_reaction(m1, MessageId {}, "x", false).encode()).has_value());
    Bytes cut = Payload::make_text(m1, "hi", 1).encode();
    cut.resize(10);
    CHECK(!Payload::decode(cut).has_value());
}

} // namespace

void Node::send(const MailboxId &id, const Bytes &wire) {
    net->push(this, id, wire);
}

int main() {
    if (sodium_init() < 0)
        return 2;
    test_basic_flow();
    test_lost_first_message_is_resent();
    test_lost_ack_does_not_duplicate();
    test_duplicated_network();
    test_offline_recipient_gets_it_after_hello();
    test_presence_can_be_switched_off();
    test_failed_messages_keep_being_tried();
    test_restart_keeps_the_conversation_and_the_queue();
    test_garbage_is_ignored();
    test_arguments_are_checked();
    test_peer_that_reinstalls_replaces_the_chat();
    test_lossy_conversation();
    test_payload_codec();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
