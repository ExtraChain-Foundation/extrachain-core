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

#include "chat/secret_session.h"

#include <sodium.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

using namespace SecretChat;

namespace {

int failures = 0;
int checks   = 0;

#define CHECK(cond)                                                                   \
    do {                                                                              \
        ++checks;                                                                     \
        if (!(cond)) {                                                                \
            ++failures;                                                               \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);               \
        }                                                                             \
    } while (0)

Bytes text(const std::string &s) {
    return Bytes(s.begin(), s.end());
}

std::string str(const Bytes &b) {
    return std::string(b.begin(), b.end());
}

IdentityKeys make_identity(std::uint8_t tag) {
    std::array<std::uint8_t, 32> seed {};
    seed.fill(tag);
    return IdentityKeys::from_seed(seed).value();
}

struct Pair {
    IdentityKeys alice;
    IdentityKeys bob;
    SignedPreKey bob_prekey;
    Session      a;
    Session      b;
};

// Alice starts a session, Bob accepts it. Both can send afterwards once Bob has answered.
Pair connect() {
    auto alice = make_identity(1);
    auto bob   = make_identity(2);
    auto spk   = SignedPreKey::generate(bob, 7).value();
    auto start = Session::initiate(alice, spk.bundle, text("hello bob"));
    CHECK(start.has_value());
    auto accepted = Session::accept(bob, spk, start->wire);
    CHECK(accepted.has_value());
    CHECK(str(accepted->plaintext) == "hello bob");
    return Pair { alice, bob, spk, std::move(start->session), std::move(accepted->session) };
}

void roundtrip(Session &from, Session &to, const std::string &msg) {
    auto wire = from.encrypt(text(msg));
    CHECK(wire.has_value());
    if (!wire.has_value())
        return;
    auto plain = to.decrypt(*wire);
    if (!plain.has_value())
        std::printf("roundtrip failed for \"%s\": error %d\n", msg.c_str(), static_cast<int>(plain.error()));
    CHECK(plain.has_value());
    if (plain.has_value())
        CHECK(str(*plain) == msg);
}

void test_conversation() {
    auto p = connect();
    CHECK(p.a.next_send_mailbox_id().has_value());
    CHECK(p.b.next_send_mailbox_id().has_value());
    roundtrip(p.b, p.a, "hi alice");
    for (int i = 0; i < 30; ++i) {
        roundtrip(p.a, p.b, "a" + std::to_string(i));
        roundtrip(p.b, p.a, "b" + std::to_string(i));
        roundtrip(p.a, p.b, "a again " + std::to_string(i));
    }
}

void test_bob_not_ready() {
    auto alice = make_identity(1);
    auto bob   = make_identity(2);
    auto spk   = SignedPreKey::generate(bob, 1).value();
    auto start = Session::initiate(alice, spk.bundle, text("x")).value();
    auto acc   = Session::accept(bob, spk, start.wire).value();
    // Bob received the first message, so the DH step gave him a sending chain
    CHECK(acc.session.encrypt(text("reply")).has_value());
}

void test_out_of_order() {
    auto p = connect();
    roundtrip(p.b, p.a, "ready");
    std::vector<Bytes> wires;
    for (int i = 0; i < 6; ++i)
        wires.push_back(p.a.encrypt(text("m" + std::to_string(i))).value());
    // Deliver in reverse
    for (int i = 5; i >= 0; --i) {
        auto plain = p.b.decrypt(wires[i]);
        CHECK(plain.has_value());
        if (plain.has_value())
            CHECK(str(*plain) == "m" + std::to_string(i));
    }
    // A key of a skipped message is spent by its first use: the same message does not open again
    for (int i = 0; i < 6; ++i)
        CHECK(!p.b.decrypt(wires[i]).has_value());
    // A chain switch with messages still in flight from the old chain
    std::vector<Bytes> old_chain;
    for (int i = 0; i < 3; ++i)
        old_chain.push_back(p.a.encrypt(text("old" + std::to_string(i))).value());
    roundtrip(p.b, p.a, "turn the ratchet");
    auto fresh = p.a.encrypt(text("new chain")).value();
    CHECK(str(p.b.decrypt(fresh).value()) == "new chain");
    for (int i = 2; i >= 0; --i) {
        auto plain = p.b.decrypt(old_chain[i]);
        CHECK(plain.has_value());
        if (plain.has_value())
            CHECK(str(*plain) == "old" + std::to_string(i));
        CHECK(!p.b.decrypt(old_chain[i]).has_value());
    }
}

void test_replay_and_tamper() {
    auto p = connect();
    roundtrip(p.b, p.a, "ready");
    auto wire = p.a.encrypt(text("once")).value();
    CHECK(p.b.decrypt(wire).has_value());
    CHECK(!p.b.decrypt(wire).has_value()); // replay

    auto good = p.a.encrypt(text("fine")).value();
    for (std::size_t pos : { std::size_t(0), std::size_t(5), std::size_t(60), std::size_t(95), good.size() - 1 }) {
        Bytes bad = good;
        bad[pos] ^= 0x40;
        CHECK(!p.b.decrypt(bad).has_value());
    }
    // None of the failures moved the ratchet: the genuine message still opens
    auto plain = p.b.decrypt(good);
    CHECK(plain.has_value());
    if (plain.has_value())
        CHECK(str(*plain) == "fine");
    CHECK(!p.b.decrypt(Bytes(10, 0)).has_value());
}

void test_skip_limit() {
    auto p = connect();
    roundtrip(p.b, p.a, "ready");
    std::vector<Bytes> wires;
    for (std::size_t i = 0; i < MaxSkip + 5; ++i)
        wires.push_back(p.a.encrypt(text("m" + std::to_string(i))).value());
    auto r = p.b.decrypt(wires.back());
    CHECK(!r.has_value());
    if (!r.has_value())
        CHECK(r.error() == Error::TooManySkipped);
    // The failed attempt left the session as it was: the earliest messages still open in order
    for (std::size_t i = 0; i < 10; ++i) {
        auto plain = p.b.decrypt(wires[i]);
        CHECK(plain.has_value());
        if (plain.has_value())
            CHECK(str(*plain) == "m" + std::to_string(i));
    }
}

void test_forward_secrecy() {
    auto p = connect();
    roundtrip(p.b, p.a, "ready");
    auto early = p.a.encrypt(text("secret one")).value();
    CHECK(p.b.decrypt(early).has_value());
    roundtrip(p.b, p.a, "reply");
    roundtrip(p.a, p.b, "more");
    // The state of Bob is stolen now; the message from before it cannot be opened with it
    auto stolen = Session::deserialize(p.b.serialize()).value();
    CHECK(!stolen.decrypt(early).has_value());
}

void test_post_compromise() {
    auto p = connect();
    roundtrip(p.b, p.a, "ready");
    roundtrip(p.a, p.b, "one");
    auto stolen = Session::deserialize(p.b.serialize()).value();
    // A full turn of the ratchet in both directions heals the session
    roundtrip(p.b, p.a, "heal 1");
    roundtrip(p.a, p.b, "heal 2");
    roundtrip(p.b, p.a, "heal 3");
    auto later = p.a.encrypt(text("after the heal")).value();
    CHECK(!stolen.decrypt(later).has_value());
    CHECK(p.b.decrypt(later).has_value());
}

void test_serialization() {
    auto p = connect();
    roundtrip(p.b, p.a, "ready");
    roundtrip(p.a, p.b, "x");
    auto wires = std::vector<Bytes> {};
    for (int i = 0; i < 4; ++i)
        wires.push_back(p.a.encrypt(text("q" + std::to_string(i))).value());
    CHECK(p.b.decrypt(wires[3]).has_value()); // leaves three skipped keys
    auto restored = Session::deserialize(p.b.serialize()).value();
    for (int i = 0; i < 3; ++i) {
        auto plain = restored.decrypt(wires[i]);
        CHECK(plain.has_value());
    }
    roundtrip(restored, p.a, "after restore");
    CHECK(!Session::deserialize(Bytes { 1, 2, 3 }).has_value());
    auto blob = p.b.serialize();
    blob.push_back(0);
    CHECK(!Session::deserialize(blob).has_value());
}

void test_padding_and_limits() {
    auto p = connect();
    roundtrip(p.b, p.a, "ready");
    std::set<std::size_t> sizes;
    for (std::size_t len : { 1u, 2u, 50u, 200u, 254u, 255u, 256u, 300u, 700u, 1500u, 2500u, 4000u, 4095u }) {
        auto wire = p.a.encrypt(Bytes(len, 'z')).value();
        sizes.insert(wire.size());
        auto plain = p.b.decrypt(wire);
        CHECK(plain.has_value() && plain->size() == len);
    }
    CHECK(sizes.size() == 5);
    auto empty = p.a.encrypt(Bytes {});
    CHECK(empty.has_value());
    if (empty.has_value()) {
        auto plain = p.b.decrypt(*empty);
        CHECK(plain.has_value() && plain->empty());
    }
    auto huge = p.a.encrypt(Bytes(MaxPlaintextSize + 1, 'z'));
    CHECK(!huge.has_value());
    if (!huge.has_value())
        CHECK(huge.error() == Error::TooLarge);
}

void test_mailbox_ids() {
    auto p = connect();
    // Bob answers first, then both talk; every id is new and the recipient expected it
    std::set<std::string> seen;
    auto record = [&](Session &from, Session &to, const std::string &msg) {
        auto id = from.next_send_mailbox_id();
        CHECK(id.has_value());
        if (!id.has_value())
            return;
        auto id_str = std::string(id->begin(), id->end());
        CHECK(seen.insert(id_str).second);
        bool expected = false;
        for (const auto &e : to.expected_receive_mailbox_ids())
            if (std::equal(e.begin(), e.end(), id->begin()))
                expected = true;
        CHECK(expected);
        roundtrip(from, to, msg);
    };
    record(p.b, p.a, "first reply");
    for (int i = 0; i < 20; ++i) {
        record(p.a, p.b, "a" + std::to_string(i));
        record(p.a, p.b, "a2" + std::to_string(i));
        record(p.b, p.a, "b" + std::to_string(i));
    }
    // The first message of a session is delivered under an id that depends on the signed prekey only
    auto alice = make_identity(1);
    auto bob   = make_identity(2);
    auto spk   = SignedPreKey::generate(bob, 3).value();
    auto s1    = Session::initiate(alice, spk.bundle, text("a")).value();
    auto s2    = Session::initiate(make_identity(9), spk.bundle, text("b")).value();
    CHECK(s1.mailbox_id == s2.mailbox_id);
    CHECK(s1.mailbox_id == initial_mailbox_id(spk.public_key));
    CHECK(s1.wire != s2.wire);
}

// A message that was lost, while later ones got through, must still be accepted when it is sent
// again: its mailbox id has to stay among the ones the recipient watches.
void test_lost_message_can_still_arrive() {
    auto p = connect();
    roundtrip(p.b, p.a, "ready");
    std::vector<MailboxId> ids;
    std::vector<Bytes>     wires;
    for (int i = 0; i < 4; ++i) {
        ids.push_back(p.a.next_send_mailbox_id().value());
        wires.push_back(p.a.encrypt(text("m" + std::to_string(i))).value());
    }
    auto watched = [&](const MailboxId &id) {
        for (const auto &e : p.b.expected_receive_mailbox_ids())
            if (e == id)
                return true;
        return false;
    };
    // Only the last one arrives
    CHECK(p.b.decrypt(wires[3]).has_value());
    CHECK(watched(ids[0]) && watched(ids[1]) && watched(ids[2]));
    CHECK(!watched(ids[3])); // consumed
    CHECK(str(p.b.decrypt(wires[1]).value()) == "m1");
    CHECK(!watched(ids[1]));
    CHECK(watched(ids[0]) && watched(ids[2]));

    // The same across a turn of the ratchet: messages of the old chain are lost, the new chain arrives
    std::vector<MailboxId> old_ids;
    std::vector<Bytes>     old_wires;
    for (int i = 0; i < 3; ++i) {
        old_ids.push_back(p.a.next_send_mailbox_id().value());
        old_wires.push_back(p.a.encrypt(text("old" + std::to_string(i))).value());
    }
    roundtrip(p.b, p.a, "turn");
    auto fresh_id = p.a.next_send_mailbox_id().value();
    auto fresh    = p.a.encrypt(text("fresh")).value();
    bool fresh_watched = false;
    for (const auto &e : p.b.expected_receive_mailbox_ids())
        if (e == fresh_id)
            fresh_watched = true;
    CHECK(fresh_watched);
    CHECK(p.b.decrypt(fresh).has_value());
    for (const auto &id : old_ids)
        CHECK(watched(id));
    CHECK(watched(ids[0]) && watched(ids[2]));
    for (int i = 2; i >= 0; --i)
        CHECK(str(p.b.decrypt(old_wires[i]).value()) == "old" + std::to_string(i));
    CHECK(str(p.b.decrypt(wires[0]).value()) == "m0");
    CHECK(str(p.b.decrypt(wires[2]).value()) == "m2");
    // Once read, nothing is watched for them any more
    for (const auto &id : old_ids)
        CHECK(!watched(id));
    // and the lists survive a restart
    (void)p.a.encrypt(text("x")).value();
    auto restored = Session::deserialize(p.b.serialize()).value();
    CHECK(restored.expected_receive_mailbox_ids().size() == p.b.expected_receive_mailbox_ids().size());
}

void test_wire_is_unlinkable() {
    auto p = connect();
    roundtrip(p.b, p.a, "ready");
    // Within one chain nothing in the header region repeats from message to message and the
    // text does not appear: the ratchet key and the counters are encrypted
    std::set<std::string> windows;
    std::size_t           total = 0;
    for (int i = 0; i < 20; ++i) {
        auto wire = p.a.encrypt(text("the very same words")).value();
        CHECK(std::search(wire.begin(), wire.end(), text("same words").begin(), text("same words").end()) == wire.end());
        for (std::size_t off = 1; off + 8 <= 89; ++off) {
            windows.insert(std::string(wire.begin() + off, wire.begin() + off + 8));
            ++total;
        }
        auto plain = p.b.decrypt(wire);
        CHECK(plain.has_value());
    }
    CHECK(windows.size() == total);
}

void test_other_session_cannot_read() {
    auto p1 = connect();
    auto p2 = connect(); // same keys, but its own ratchet
    roundtrip(p1.b, p1.a, "ready");
    roundtrip(p2.b, p2.a, "ready");
    auto wire = p1.a.encrypt(text("for pair one")).value();
    CHECK(!p2.b.decrypt(wire).has_value());
    CHECK(p1.b.decrypt(wire).has_value());
}

void test_handshake_failures() {
    auto alice = make_identity(1);
    auto bob   = make_identity(2);
    auto spk   = SignedPreKey::generate(bob, 5).value();

    // A bundle with a forged signature is refused
    auto forged = spk.bundle;
    forged.signature[3] ^= 1;
    CHECK(!forged.verify());
    auto r = Session::initiate(alice, forged, text("x"));
    CHECK(!r.has_value());
    if (!r.has_value())
        CHECK(r.error() == Error::BadSignature);

    // A bundle whose prekey was swapped fails the signature too
    auto swapped = spk.bundle;
    swapped.signed_prekey[0] ^= 1;
    CHECK(!swapped.verify());

    auto start = Session::initiate(alice, spk.bundle, text("x")).value();
    // Someone who does not own the prekey cannot open the first message
    auto other = SignedPreKey::generate(bob, 5).value();
    CHECK(!Session::accept(bob, other, start.wire).has_value());
    // A prekey with another id is not the one addressed
    auto wrong_id = spk;
    wrong_id.id   = 6;
    CHECK(!Session::accept(bob, wrong_id, start.wire).has_value());
    // A damaged first message
    Bytes bad = start.wire;
    bad[bad.size() / 2] ^= 1;
    CHECK(!Session::accept(bob, spk, bad).has_value());
    CHECK(Session::accept(bob, spk, start.wire).has_value());

    // Bundles survive their wire form
    auto parsed = PreKeyBundle::parse(spk.bundle.serialize());
    CHECK(parsed.has_value() && parsed->verify());
    CHECK(!PreKeyBundle::parse(Bytes(10, 0)).has_value());
}

void test_safety_number() {
    auto a = make_identity(1);
    auto b = make_identity(2);
    auto c = make_identity(3);
    auto n1 = safety_number(a.public_key, b.public_key);
    auto n2 = safety_number(b.public_key, a.public_key);
    auto n3 = safety_number(a.public_key, c.public_key);
    CHECK(n1 == n2);
    CHECK(n1 != n3);
    CHECK(n1.size() == 12 * 5 + 11);
    for (char ch : n1)
        CHECK(ch == ' ' || (ch >= '0' && ch <= '9'));
    auto p = connect();
    CHECK(p.a.safety_number() == p.b.safety_number());
    CHECK(p.a.safety_number() == safety_number(p.alice.public_key, p.bob.public_key));
}

void test_custom_payload() {
    for (std::size_t len : { std::size_t(0), std::size_t(1), std::size_t(255), std::size_t(256), std::size_t(4400),
                             std::size_t(65535), std::size_t(65536), std::size_t(70000) }) {
        Bytes envelope(len);
        randombytes_buf(envelope.data(), envelope.size());
        const Bytes wrapped = custom_payload(envelope);
        CHECK(wrapped[0] == 0xc4 || wrapped[0] == 0xc5 || wrapped[0] == 0xc6);
        auto opened = open_custom_payload(wrapped);
        CHECK(opened.has_value() && *opened == envelope);
        // A wrong length or trailing bytes are not accepted
        Bytes longer = wrapped;
        longer.push_back(0);
        CHECK(!open_custom_payload(longer).has_value());
        if (!wrapped.empty()) {
            Bytes shorter = wrapped;
            shorter.pop_back();
            CHECK(!open_custom_payload(shorter).has_value());
        }
    }
    CHECK(!open_custom_payload(Bytes {}).has_value());
    CHECK(!open_custom_payload(Bytes { 0x92, 1, 2 }).has_value()); // a MessagePack array
    CHECK(!open_custom_payload(Bytes { 0xc4 }).has_value());
    CHECK(!open_custom_payload(Bytes { 0xc6, 0, 0 }).has_value());
}

} // namespace

int main() {
    if (sodium_init() < 0)
        return 2;
    test_conversation();
    test_bob_not_ready();
    test_out_of_order();
    test_replay_and_tamper();
    test_skip_limit();
    test_forward_secrecy();
    test_post_compromise();
    test_serialization();
    test_padding_and_limits();
    test_mailbox_ids();
    test_lost_message_can_still_arrive();
    test_wire_is_unlinkable();
    test_other_session_cannot_read();
    test_handshake_failures();
    test_safety_number();
    test_custom_payload();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
