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

#include "chain/pack.h"
#include "chain/pack_registry.h"
#include "chat/chat_manager.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "encryption/encryption_tools.h"
#include "managers/account_controller.h"
#include "utils/compression.h"
#include "utils/exc_logs.h"
#include "test_support.h"
#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>

class Test {
public:
    ~Test() {
        if (node) {
            node->cleanUp();
        }
    }

    std::unique_ptr<ExtraChain::Core::ExtraChainNode> node;

    void actors() {
        Actor<KeyPrivate> actor1;
        Actor<KeyPrivate> actor2;
        actor1.create(ActorType::User);
        actor2.create(ActorType::User);

        const auto message   = ByteArray("hello").toBytes();
        const auto encrypted = actor1.key().encrypt(message, actor2.key().public_key());
        TEST_REQUIRE(encrypted.has_value());
        const auto decrypted = actor2.key().decrypt(encrypted.value(), actor1.key().public_key());
        TEST_REQUIRE(decrypted.has_value());
        TEST_REQUIRE_EQ(decrypted.value(), message);
    }

    void mnemonicRoundTrip() {
        MasterSeed seed;
        for (std::size_t i = 0; i < seed.size(); ++i) {
            seed[i] = static_cast<std::uint8_t>(i + 1);
        }

        const auto mnemonic = Cryptography::create_mnemonic(seed);
        TEST_REQUIRE_EQ(mnemonic.size(), std::size_t(24));

        std::ostringstream phrase;
        for (std::size_t i = 0; i < mnemonic.size(); ++i) {
            if (i != 0) {
                phrase << ' ';
            }
            phrase << mnemonic[i];
        }

        const auto phraseText = phrase.str();
        TEST_REQUIRE(Cryptography::validate_mnemonic(phraseText));

        const auto restored = Cryptography::restore_seed_from_mnemonic(phraseText);
        TEST_REQUIRE(restored.has_value());
        TEST_REQUIRE(restored.value() == seed);
    }

    void defaultProfileDoesNotExportMnemonic() {
        AccountController accounts(nullptr);
        TEST_REQUIRE(accounts.seed_mnemonic().empty());
    }

    void bigNumberDecimalRoundtrip() {
        // Decimal string canonical form
        TEST_REQUIRE_EQ(BigNumber("12345").to_string(), std::string("12345"));
        TEST_REQUIRE_EQ(BigNumber(0).to_string(), std::string("0"));
        TEST_REQUIRE_EQ(BigNumber(-42).to_string(), std::string("-42"));

        // Large numbers that don't fit in 64-bit
        BigNumber big("123456789012345678901234567890");
        TEST_REQUIRE_EQ(big.to_string(), std::string("123456789012345678901234567890"));

        // Leading zeros stripped
        TEST_REQUIRE_EQ(BigNumber("00042").to_string(), std::string("42"));
        TEST_REQUIRE_EQ(BigNumber("-00042").to_string(), std::string("-42"));
    }

    void bigNumberHexFallback() {
        // Hex strings auto-detected (backward compat with pre-decimal chain)
        TEST_REQUIRE(BigNumber::is_hex_string("abc123"));
        TEST_REQUIRE(BigNumber::is_hex_string("FFFF"));
        TEST_REQUIRE(!BigNumber::is_hex_string("12345"));
        TEST_REQUIRE(!BigNumber::is_hex_string(""));
        TEST_REQUIRE(!BigNumber::is_hex_string("-"));

        // from_hex explicit — hex is decoded only when asked for explicitly.
        TEST_REQUIRE_EQ(BigNumber::from_hex("ff").to_string(), std::string("255"));
        TEST_REQUIRE_EQ(BigNumber::from_hex("100").to_string(), std::string("256"));
        TEST_REQUIRE_EQ(BigNumber::from_hex("-ff").to_string(), std::string("-255"));

        // The string constructor is strict decimal — it never sniffs hex (that
        // was ambiguous: "100" is both a decimal and a hex value). So an all-digit
        // string is always decimal, and "100" stays 100.
        TEST_REQUIRE_EQ(BigNumber("100").to_string(), std::string("100"));
        TEST_REQUIRE_EQ(BigNumber("255").to_string(), std::string("255"));
    }

    void bigNumberHexWriteRead() {
        // to_hex_string / from_hex round trip
        BigNumber a(255);
        TEST_REQUIRE_EQ(a.to_hex_string(), std::string("ff"));
        TEST_REQUIRE_EQ(BigNumber::from_hex(a.to_hex_string()).to_string(), a.to_string());

        BigNumber b("999999999999999");
        auto      hex = b.to_hex_string();
        TEST_REQUIRE_EQ(BigNumber::from_hex(hex).to_string(), b.to_string());
    }

    void bigNumberFloatDecimal() {
        TEST_REQUIRE_EQ(BigNumberFloat("1.5").to_string(), std::string("1.5"));
        TEST_REQUIRE_EQ(BigNumberFloat("0.0011").to_string(), std::string("0.0011"));
        TEST_REQUIRE_EQ(BigNumberFloat("1000000000000").to_string(), std::string("1000000000000"));

        BigNumberFloat a("1.5");
        BigNumberFloat b("2.5");
        TEST_REQUIRE_EQ((a + b).to_string(), std::string("4"));
        TEST_REQUIRE_EQ((b - a).to_string(), std::string("1"));
    }

    void compressionRoundtrip() {
        std::string data =
            "the quick brown fox jumps over the lazy dog "
            "the quick brown fox jumps over the lazy dog "
            "the quick brown fox jumps over the lazy dog";
        auto compressed = Compression::compress(data);
        TEST_REQUIRE(compressed.has_value());
        TEST_REQUIRE(compressed->size() < data.size());

        auto back = Compression::decompress(*compressed);
        TEST_REQUIRE(back.has_value());
        TEST_REQUIRE_EQ(*back, data);
    }

    void compressionWithDict() {
        std::string dict =
            R"({"section":"","type":0,"sender":"","receiver":"","token":"","amount":"","timestamp":0,"prev_hashs":[],"hash":"","signature":""})";
        std::string data =
            R"({"section":"42","type":0,"sender":"29c35573f5ff57b956de44a942f579bbc9843b13","receiver":"29c35573f5ff57b956de44a942f579bbc9843b13","token":"0000000000000000000000000000000000000000","amount":"100","timestamp":1774951775152,"prev_hashs":[],"hash":"b1abb1f1","signature":"xZg"})";

        auto no_dict = Compression::compress(data);
        TEST_REQUIRE(no_dict.has_value());

        auto with_dict = Compression::compress(data, dict);
        TEST_REQUIRE(with_dict.has_value());
        // Dictionary should improve ratio on small structured payloads
        TEST_REQUIRE(with_dict->size() <= no_dict->size());

        auto back = Compression::decompress(*with_dict, dict);
        TEST_REQUIRE(back.has_value());
        TEST_REQUIRE_EQ(*back, data);
    }

    void compressionContextReuse() {
        std::string          dict = R"({"section":"","type":0,"sender":"","receiver":""})";
        Compression::Context ctx(dict);

        for (int i = 0; i < 10; i++) {
            std::string data =
                R"({"section":")" + std::to_string(i) + R"(","type":0,"sender":"abc","receiver":"def"})";
            auto compressed = ctx.compress_frame(data);
            TEST_REQUIRE(compressed.has_value());

            auto back = ctx.decompress_frame(*compressed);
            TEST_REQUIRE(back.has_value());
            TEST_REQUIRE_EQ(*back, data);
        }
    }

    void compressionEmptyInput() {
        auto compressed = Compression::compress("");
        TEST_REQUIRE(compressed.has_value());
        auto back = Compression::decompress(*compressed);
        TEST_REQUIRE(back.has_value());
        TEST_REQUIRE_EQ(*back, std::string(""));
    }

    void compressionInvalidData() {
        std::string garbage = "this is not zstd data";
        auto        result  = Compression::decompress(garbage);
        TEST_REQUIRE(!result.has_value());
    }

    void packRoundTrip100() {
        std::filesystem::path p = std::filesystem::temp_directory_path() / "exc_pack_rt.pack";
        std::filesystem::remove(p);

        std::map<SectionId, std::string> input;
        for (int i = 0; i < 100; i++) {
            input.emplace(SectionId(i),
                          R"({"section":")" + std::to_string(i)
                              + R"(","type":0,"sender":"29c35573f5ff57b956de44a942f579bbc9843b13","amount":")"
                              + std::to_string(i * 100) + R"("})");
        }

        auto w = Pack::write(p, 0, input);
        TEST_REQUIRE_MESSAGE(w.has_value(), "Pack::write failed");

        auto r = Pack::Reader::open(p);
        TEST_REQUIRE_MESSAGE(r.has_value(), "Pack::Reader::open failed");

        TEST_REQUIRE_EQ(r->count(), static_cast<std::size_t>(100));
        TEST_REQUIRE_EQ(r->first_section(), SectionId(0));
        TEST_REQUIRE_EQ(r->last_section(), SectionId(99));

        for (int i = 0; i < 100; i++) {
            auto got = r->read(SectionId(i));
            TEST_REQUIRE_MESSAGE(got.has_value(), std::string("read ") + std::to_string(i));
            TEST_REQUIRE_EQ(*got, input[SectionId(i)]);
        }

        std::filesystem::remove(p);
    }

    void packRandomAccess() {
        std::filesystem::path p = std::filesystem::temp_directory_path() / "exc_pack_rand.pack";
        std::filesystem::remove(p);

        std::map<SectionId, std::string> input;
        for (int i = 50; i < 250; i++) {
            input.emplace(SectionId(i), std::string("payload-") + std::to_string(i));
        }

        auto w = Pack::write(p, 42, input);
        TEST_REQUIRE(w.has_value());

        auto r = Pack::Reader::open(p);
        TEST_REQUIRE(r.has_value());

        TEST_REQUIRE_EQ(r->id(), static_cast<Pack::PackId>(42));
        TEST_REQUIRE_EQ(r->first_section(), SectionId(50));
        TEST_REQUIRE_EQ(r->last_section(), SectionId(249));

        // Out of range
        TEST_REQUIRE(!r->read(SectionId(49)).has_value());
        TEST_REQUIRE(!r->read(SectionId(250)).has_value());

        // Random sample
        for (int id : { 50, 100, 199, 249, 75 }) {
            auto got = r->read(SectionId(id));
            TEST_REQUIRE(got.has_value());
            TEST_REQUIRE_EQ(*got, std::string("payload-") + std::to_string(id));
        }

        std::filesystem::remove(p);
    }

    void packReadRange() {
        std::filesystem::path p = std::filesystem::temp_directory_path() / "exc_pack_range.pack";
        std::filesystem::remove(p);

        std::map<SectionId, std::string> input;
        for (int i = 0; i < 200; i++) {
            input.emplace(SectionId(i), std::string("item_") + std::to_string(i));
        }

        auto w = Pack::write(p, 0, input);
        TEST_REQUIRE(w.has_value());

        auto r = Pack::Reader::open(p);
        TEST_REQUIRE(r.has_value());

        auto rng = r->read_range(SectionId(75), SectionId(125));
        TEST_REQUIRE_EQ(rng.size(), static_cast<std::size_t>(51));
        for (std::size_t i = 0; i < rng.size(); i++) {
            int id = 75 + static_cast<int>(i);
            TEST_REQUIRE_EQ(rng[i].first, SectionId(id));
            TEST_REQUIRE_EQ(rng[i].second, std::string("item_") + std::to_string(id));
        }

        std::filesystem::remove(p);
    }

    void packNonConsecutive() {
        std::filesystem::path p = std::filesystem::temp_directory_path() / "exc_pack_nc.pack";
        std::filesystem::remove(p);

        std::map<SectionId, std::string> input;
        input.emplace(SectionId(0), "a");
        input.emplace(SectionId(2), "c"); // gap
        input.emplace(SectionId(3), "d");

        auto w = Pack::write(p, 0, input);
        TEST_REQUIRE(!w.has_value());
        TEST_REQUIRE_EQ(w.error(), Pack::Error::NonConsecutiveSections);

        std::filesystem::remove(p);
    }

    void packCorruption() {
        std::filesystem::path p = std::filesystem::temp_directory_path() / "exc_pack_corrupt.pack";
        std::filesystem::remove(p);

        std::map<SectionId, std::string> input;
        input.emplace(SectionId(0), "hello");
        auto w = Pack::write(p, 0, input);
        TEST_REQUIRE(w.has_value());

        // Corrupt the file middle
        {
            std::fstream f(p, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(120);
            char bad = '\xFF';
            f.write(&bad, 1);
        }

        auto r = Pack::Reader::open(p);
        TEST_REQUIRE(!r.has_value());

        std::filesystem::remove(p);
    }

    void packEmpty() {
        std::filesystem::path            p = std::filesystem::temp_directory_path() / "exc_pack_empty.pack";
        std::map<SectionId, std::string> input;
        auto                             w = Pack::write(p, 0, input);
        TEST_REQUIRE(!w.has_value());
        TEST_REQUIRE_EQ(w.error(), Pack::Error::EmptyInput);
    }

    void packRegistryMultiPack() {
        auto dir = std::filesystem::temp_directory_path() / "exc_registry_test";
        std::filesystem::remove_all(dir);

        Pack::Registry reg(dir, 4);

        // Create 3 packs: [0-99], [100-199], [200-299]
        for (int p = 0; p < 3; p++) {
            std::map<SectionId, std::string> input;
            for (int i = 0; i < 100; i++) {
                int sid = p * 100 + i;
                input.emplace(SectionId(sid), "sec-" + std::to_string(sid));
            }
            auto w = reg.create_pack(p, input);
            TEST_REQUIRE_MESSAGE(w.has_value(), std::string("pack ") + std::to_string(p));
        }

        // Read from each pack
        auto s0 = reg.read_section(SectionId(5));
        TEST_REQUIRE(s0.has_value());
        TEST_REQUIRE_EQ(*s0, std::string("sec-5"));

        auto s1 = reg.read_section(SectionId(150));
        TEST_REQUIRE(s1.has_value());
        TEST_REQUIRE_EQ(*s1, std::string("sec-150"));

        auto s2 = reg.read_section(SectionId(299));
        TEST_REQUIRE(s2.has_value());
        TEST_REQUIRE_EQ(*s2, std::string("sec-299"));

        // Outside range
        TEST_REQUIRE(!reg.read_section(SectionId(300)).has_value());

        // Range that spans packs
        auto range = reg.read_sections(SectionId(95), SectionId(105));
        TEST_REQUIRE_EQ(range.size(), static_cast<std::size_t>(11));
        TEST_REQUIRE_EQ(range.front().first, SectionId(95));
        TEST_REQUIRE_EQ(range.back().first, SectionId(105));

        // Coverage
        auto cov = reg.coverage();
        TEST_REQUIRE(cov.has_value());
        TEST_REQUIRE_EQ(cov->first, SectionId(0));
        TEST_REQUIRE_EQ(cov->last, SectionId(299));

        // Rescan preserves state
        reg.rescan();
        auto s3 = reg.read_section(SectionId(123));
        TEST_REQUIRE(s3.has_value());
        TEST_REQUIRE_EQ(*s3, std::string("sec-123"));

        std::filesystem::remove_all(dir);
    }

    void packRegistryLazyOpen() {
        auto dir = std::filesystem::temp_directory_path() / "exc_registry_lazy";
        std::filesystem::remove_all(dir);

        Pack::Registry reg(dir, 2); // small LRU

        // Create 5 packs
        for (int p = 0; p < 5; p++) {
            std::map<SectionId, std::string> input;
            for (int i = 0; i < 10; i++) {
                int sid = p * 10 + i;
                input.emplace(SectionId(sid), "v" + std::to_string(sid));
            }
            auto w = reg.create_pack(p, input);
            TEST_REQUIRE(w.has_value());
        }

        // Touch all, LRU should keep most recent 2 open
        for (int sid : { 5, 15, 25, 35, 45, 5 }) {
            auto s = reg.read_section(SectionId(sid));
            TEST_REQUIRE(s.has_value());
            TEST_REQUIRE_EQ(*s, "v" + std::to_string(sid));
        }

        std::filesystem::remove_all(dir);
    }

    void createNetwork() {
        std::filesystem::create_directories("test-data");
        std::filesystem::current_path("test-data");
        Utils::wipeDataFiles();
        node = std::make_unique<ExtraChain::Core::ExtraChainNode>();
        node->process();
        const bool is_created = node->create_new_network("login", "password");
        TEST_REQUIRE(is_created);
        TEST_REQUIRE(node->create_chat_templates());
    }

    void chatJsonRemainsBackwardCompatible() {
        Actor<KeyPrivate> owner;
        owner.create(ActorType::User);

        Chat::Chat chat { .id = "row-id", .owner_id = owner.id(), .file_id = "chat-file" };
        auto       json = Json::serialize_value(chat).as_object();
        json.erase("invite_pending");

        auto restored = Json::deserialize<Chat::Chat>(boost::json::serialize(json));
        TEST_REQUIRE(restored.has_value());
        TEST_REQUIRE_EQ(restored->id, chat.id);
        TEST_REQUIRE_EQ(restored->owner_id, chat.owner_id);
        TEST_REQUIRE_EQ(restored->file_id, chat.file_id);
        TEST_REQUIRE(!restored->invite_pending);
    }

    void chatCreationPersistsBeforeSuccess() {
        TEST_REQUIRE(node != nullptr);

        auto activation = node->chat_manager()->activate();
        TEST_REQUIRE(activation.has_value());

        Actor<KeyPrivate> peer;
        peer.create(ActorType::User);

        auto created = node->chat_manager()->create_dialogue(peer.id());
        TEST_REQUIRE(created.has_value());
        const auto& created_chat = created.value();
        TEST_REQUIRE(!created_chat.id.empty());
        TEST_REQUIRE(!created_chat.owner_id.is_zero());
        TEST_REQUIRE(!created_chat.file_id.empty());

        auto stored = node->chat_manager()->read_chats();
        TEST_REQUIRE(stored.has_value());
        const auto& stored_chats = stored.value();
        auto        persisted =
            std::find_if(stored_chats.cbegin(), stored_chats.cend(), [&created_chat](const auto& chat) {
                return chat.id == created_chat.id && chat.owner_id == created_chat.owner_id
                       && chat.file_id == created_chat.file_id;
            });
        TEST_REQUIRE(persisted != stored_chats.cend());
        TEST_REQUIRE_EQ(persisted->invite_pending, created_chat.invite_pending);
    }

    void targetedActorRefreshReportsNoActivePeers() {
        TEST_REQUIRE(node != nullptr);

        Actor<KeyPrivate> actor;
        actor.create(ActorType::User);

        TEST_REQUIRE(!node->dfs()->refresh_actors({ actor.id(), actor.id(), ActorId() }));
    }

    void vectorRowRebroadcastReportsNoActivePeers() {
        TEST_REQUIRE(node != nullptr);
        TEST_REQUIRE(!node->dfs()->rebroadcast_vector_row(ActorId(), "missing", "missing"));
    }

    void blocks() {
        // Reserved for block compatibility cases.
    }
};

int main() {
    Test                tests;
    TestSupport::Runner runner;
    runner.run("actors", [&] {
        tests.actors();
    });
    runner.run("mnemonic round trip", [&] {
        tests.mnemonicRoundTrip();
    });
    runner.run("default profile mnemonic", [&] {
        tests.defaultProfileDoesNotExportMnemonic();
    });
    runner.run("decimal big number", [&] {
        tests.bigNumberDecimalRoundtrip();
    });
    runner.run("hex big number fallback", [&] {
        tests.bigNumberHexFallback();
    });
    runner.run("hex big number round trip", [&] {
        tests.bigNumberHexWriteRead();
    });
    runner.run("decimal big number float", [&] {
        tests.bigNumberFloatDecimal();
    });
    runner.run("compression round trip", [&] {
        tests.compressionRoundtrip();
    });
    runner.run("compression dictionary", [&] {
        tests.compressionWithDict();
    });
    runner.run("compression context reuse", [&] {
        tests.compressionContextReuse();
    });
    runner.run("compression empty input", [&] {
        tests.compressionEmptyInput();
    });
    runner.run("compression invalid data", [&] {
        tests.compressionInvalidData();
    });
    runner.run("pack round trip", [&] {
        tests.packRoundTrip100();
    });
    runner.run("pack random access", [&] {
        tests.packRandomAccess();
    });
    runner.run("pack range", [&] {
        tests.packReadRange();
    });
    runner.run("pack non-consecutive input", [&] {
        tests.packNonConsecutive();
    });
    runner.run("pack corruption", [&] {
        tests.packCorruption();
    });
    runner.run("pack empty input", [&] {
        tests.packEmpty();
    });
    runner.run("pack registry", [&] {
        tests.packRegistryMultiPack();
    });
    runner.run("pack registry lazy open", [&] {
        tests.packRegistryLazyOpen();
    });
    runner.run("network creation", [&] {
        tests.createNetwork();
    });
    runner.run("chat JSON compatibility", [&] {
        tests.chatJsonRemainsBackwardCompatible();
    });
    runner.run("chat creation persistence", [&] {
        tests.chatCreationPersistsBeforeSuccess();
    });
    runner.run("targeted actor refresh", [&] {
        tests.targetedActorRefreshReportsNoActivePeers();
    });
    runner.run("vector row rebroadcast", [&] {
        tests.vectorRowRebroadcastReportsNoActivePeers();
    });
    return runner.result();
}
