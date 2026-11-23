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

// An application that was released before secret chats reads every Custom message it receives
// as its own payload. This checks, with the real MessagePack parser, that such a reader refuses
// a secret chat envelope with an exception it catches, never takes it for its own message, and
// still reads the tagged CustomMessage frame the way it read the untagged one.

#include "chat/secret_session.h"

#include <msgpack.hpp>
#include <sodium.h>

#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

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

// The shapes an application reads Custom payloads as: wide array structs of mixed fields,
// a map of maps, and a bare boolean.
struct WideMessage {
    bool                  a = false;
    bool                  b = false;
    std::string           c;
    std::set<std::string> d;
    std::string           e;
    int                   f = 0;
    int                   g = 0;
    int                   h = -1;
    std::set<int>         i;
    std::string           j;
    int                   k = 0;
    std::string           l;
    MSGPACK_DEFINE(a, b, c, d, e, f, g, h, i, j, k, l)
};
struct CountryMessage {
    std::string actor;
    std::string country;
    bool        status = false;
    int         type   = 0;
    MSGPACK_DEFINE(actor, country, status, type)
};
struct CapabilityMessage {
    std::string kind;
    std::string actor;
    std::string country;
    bool        status = false;
    int         type   = 0;
    bool        google = false;
    bool        yandex = false;
    MSGPACK_DEFINE(kind, actor, country, status, type, google, yandex)
};
using ServerMap = std::map<std::string, std::map<std::string, std::pair<bool, int>>>;

template <class T>
bool parses_as(const Bytes &data) {
    try {
        auto handle = msgpack::unpack(reinterpret_cast<const char *>(data.data()), data.size());
        (void)handle.get().as<T>();
        return true;
    } catch (const std::exception &) {
        return false;
    }
}

// Reads the Custom frame the way a node released before the application tag does
struct LegacyCustom {
    std::string owner;
    std::string data;
    MSGPACK_DEFINE(owner, data)
};
struct TaggedCustom {
    std::string owner;
    std::string data;
    std::string app;
    MSGPACK_DEFINE(owner, data, app)
};

} // namespace

int main() {
    if (sodium_init() < 0)
        return 2;

    for (int round = 0; round < 20000; ++round) {
        Bytes envelope(1 + randombytes_uniform(4400));
        randombytes_buf(envelope.data(), envelope.size());
        const Bytes payload = custom_payload(envelope);
        CHECK(!parses_as<WideMessage>(payload));
        CHECK(!parses_as<CountryMessage>(payload));
        CHECK(!parses_as<CapabilityMessage>(payload));
        CHECK(!parses_as<ServerMap>(payload));
        CHECK(!parses_as<bool>(payload));
    }

    // A real envelope too
    std::array<std::uint8_t, 32> seed {};
    seed.fill(5);
    auto alice = IdentityKeys::from_seed(seed).value();
    seed.fill(6);
    auto bob   = IdentityKeys::from_seed(seed).value();
    auto spk   = SignedPreKey::generate(bob, 1).value();
    auto start = Session::initiate(alice, spk.bundle, Bytes { 'h', 'i' }).value();
    const Bytes payload = custom_payload(start.wire);
    CHECK(!parses_as<WideMessage>(payload));
    CHECK(!parses_as<CountryMessage>(payload));
    CHECK(!parses_as<CapabilityMessage>(payload));
    CHECK(!parses_as<ServerMap>(payload));
    CHECK(!parses_as<bool>(payload));

    // The frame: an older reader sees owner and data of a tagged message, a newer one sees an
    // empty tag in an older message
    TaggedCustom tagged { "owner", std::string(payload.begin(), payload.end()), std::string("secret-chat/1") };
    msgpack::sbuffer tagged_buf;
    msgpack::pack(tagged_buf, tagged);
    try {
        auto handle = msgpack::unpack(tagged_buf.data(), tagged_buf.size());
        LegacyCustom legacy;
        handle.get().convert(legacy);
        CHECK(legacy.owner == "owner");
        CHECK(legacy.data == tagged.data);
    } catch (const std::exception &) {
        CHECK(false);
    }
    LegacyCustom old_message { "owner", "payload" };
    msgpack::sbuffer old_buf;
    msgpack::pack(old_buf, old_message);
    try {
        auto         handle = msgpack::unpack(old_buf.data(), old_buf.size());
        TaggedCustom now;
        handle.get().convert(now);
        CHECK(now.owner == "owner" && now.data == "payload" && now.app.empty());
    } catch (const std::exception &) {
        CHECK(false);
    }

    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
