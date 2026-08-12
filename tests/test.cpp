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
#include "encryption/encryption_tools.h"
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "utils/compression.h"
#include "utils/exc_logs.h"
#include "adapters/qt/logging_adapter.h"
#include <QtTest/QtTest>
#include <algorithm>
#include <filesystem>
#include <sstream>

class Test : public QObject {
    Q_OBJECT

public:
    Test(QObject *parent = nullptr)
        : QObject(parent) {
    }

private:
    ExtraChainNode *node;

private slots:
    void actors() {
        Actor<KeyPrivate> actor1;
        Actor<KeyPrivate> actor2;
        actor1.create(ActorType::Wallet);
        actor2.create(ActorType::Wallet);

        auto encrypted = actor1.key()->encrypt("hello", actor2.key()->publicKey());
        auto decrypted = actor2.key()->decrypt(encrypted, actor1.key()->publicKey());

        auto actor1Public = actor1.convertToPublic();

        auto encrypted2 = actor1Public.key()->encrypt("hello", actor2.key()->secretKey());
        auto decrypted2 = actor2.key()->decrypt(encrypted2, actor1.key()->publicKey());

        QCOMPARE(decrypted, decrypted2);
    }

    void mnemonicRoundTrip() {
        MasterSeed seed;
        for (std::size_t i = 0; i < seed.size(); ++i) {
            seed[i] = static_cast<std::uint8_t>(i + 1);
        }

        const auto mnemonic = Cryptography::create_mnemonic(seed);
        QCOMPARE(mnemonic.size(), std::size_t(24));

        std::ostringstream phrase;
        for (std::size_t i = 0; i < mnemonic.size(); ++i) {
            if (i != 0) {
                phrase << ' ';
            }
            phrase << mnemonic[i];
        }

        const auto phraseText = phrase.str();
        QVERIFY(Cryptography::validate_mnemonic(phraseText));

        const auto restored = Cryptography::restore_seed_from_mnemonic(phraseText);
        QVERIFY(restored.has_value());
        QVERIFY(restored.value() == seed);
    }

    void defaultProfileDoesNotExportMnemonic() {
        AccountController accounts(nullptr);
        QVERIFY(accounts.seed_mnemonic().empty());
    }

    //    auto key1 =
    //        KeyPrivate("0c6b88536b8e82af9080650ee7fc02bc721d6b9dce7b9f31513ddf6611154ad3517a72717814ff418628"
    //                   "b11648731f4ecd16828b52d7752e3af15e1f10991b3a",
    //                   "517a72717814ff418628b11648731f4ecd16828b52d7752e3af15e1f10991b3a");
    //    auto key2 =
    //    KeyPrivate("aef07e6dbd4874a881a511bf0c052a901480fa79ea748bf0bb6092ce37ea959c53f0dda754236784d"
    //                           "729f27b9ed033d90bb17bb7d78a36fabac53130422c06e6",
    //                           "53f0dda754236784d729f27b9ed033d90bb17bb7d78a36fabac53130422c06e6");

    //    const QByteArray message = "Something";
    //    auto sign1 = key1.sign(message);
    //    auto sign2 = key2.sign(message);
    //    eLog("Sign1: {}", sign1);
    //    eLog("Sign2: {}", sign2);

    //    auto verify1 = key1.verify(message, sign1);
    //    auto verify2 = key2.verify(message, sign2);
    //    auto verify1Test = key1.verify(message, sign2);
    //    auto verify2Test = key2.verify(message, sign1);
    //    eLog("Verify: {} {} {} {}", verify1, verify2, verify1Test, verify2Test);

    //    auto encrypted = key1.encrypt(message, key2.publicKey());
    //    eLog("Encrypt: {}", encrypted);
    //    auto decrypted = key2.decrypt(encrypted, key1.publicKey());
    //    eLog("Decrypt: {}", decrypted);
    //    return 0;
    //    KeyPrivate key;
    //    QByteArray data = QByteArray("qweqe").repeated(30000);
    //    eLog("{}", data.size());

    //    while (true) {
    //        QElapsedTimer timer;
    //        timer.start();
    //        for (int i = 0; i != 10000; i++)
    //            key.sign(data);
    //        eLog("{} ms", timer.elapsed());
    //    }

    void bigNumberDecimalRoundtrip() {
        // Decimal string canonical form
        QCOMPARE(BigNumber("12345").to_string(), std::string("12345"));
        QCOMPARE(BigNumber(0).to_string(), std::string("0"));
        QCOMPARE(BigNumber(-42).to_string(), std::string("-42"));

        // Large numbers that don't fit in 64-bit
        BigNumber big("123456789012345678901234567890");
        QCOMPARE(big.to_string(), std::string("123456789012345678901234567890"));

        // Leading zeros stripped
        QCOMPARE(BigNumber("00042").to_string(), std::string("42"));
        QCOMPARE(BigNumber("-00042").to_string(), std::string("-42"));
    }

    void bigNumberHexFallback() {
        // Hex strings auto-detected (backward compat with pre-decimal chain)
        QVERIFY(BigNumber::is_hex_string("abc123"));
        QVERIFY(BigNumber::is_hex_string("FFFF"));
        QVERIFY(!BigNumber::is_hex_string("12345"));
        QVERIFY(!BigNumber::is_hex_string(""));
        QVERIFY(!BigNumber::is_hex_string("-"));

        // from_hex explicit — hex is decoded only when asked for explicitly.
        QCOMPARE(BigNumber::from_hex("ff").to_string(), std::string("255"));
        QCOMPARE(BigNumber::from_hex("100").to_string(), std::string("256"));
        QCOMPARE(BigNumber::from_hex("-ff").to_string(), std::string("-255"));

        // The string constructor is strict decimal — it never sniffs hex (that
        // was ambiguous: "100" is both a decimal and a hex value). So an all-digit
        // string is always decimal, and "100" stays 100.
        QCOMPARE(BigNumber("100").to_string(), std::string("100"));
        QCOMPARE(BigNumber("255").to_string(), std::string("255"));
    }

    void bigNumberHexWriteRead() {
        // to_hex_string / from_hex round trip
        BigNumber a(255);
        QCOMPARE(a.to_hex_string(), std::string("ff"));
        QCOMPARE(BigNumber::from_hex(a.to_hex_string()).to_string(), a.to_string());

        BigNumber b("999999999999999");
        auto hex = b.to_hex_string();
        QCOMPARE(BigNumber::from_hex(hex).to_string(), b.to_string());
    }

    void bigNumberFloatDecimal() {
        QCOMPARE(BigNumberFloat("1.5").to_string(), std::string("1.5"));
        QCOMPARE(BigNumberFloat("0.0011").to_string(), std::string("0.0011"));
        QCOMPARE(BigNumberFloat("1000000000000").to_string(), std::string("1000000000000"));

        BigNumberFloat a("1.5");
        BigNumberFloat b("2.5");
        QCOMPARE((a + b).to_string(), std::string("4"));
        QCOMPARE((b - a).to_string(), std::string("1"));
    }

    void compressionRoundtrip() {
        std::string data = "the quick brown fox jumps over the lazy dog "
                           "the quick brown fox jumps over the lazy dog "
                           "the quick brown fox jumps over the lazy dog";
        auto compressed = Compression::compress(data);
        QVERIFY(compressed.has_value());
        QVERIFY(compressed->size() < data.size());

        auto back = Compression::decompress(*compressed);
        QVERIFY(back.has_value());
        QCOMPARE(*back, data);
    }

    void compressionWithDict() {
        std::string dict = R"({"section":"","type":0,"sender":"","receiver":"","token":"","amount":"","timestamp":0,"prev_hashs":[],"hash":"","signature":""})";
        std::string data = R"({"section":"42","type":0,"sender":"29c35573f5ff57b956de44a942f579bbc9843b13","receiver":"29c35573f5ff57b956de44a942f579bbc9843b13","token":"0000000000000000000000000000000000000000","amount":"100","timestamp":1774951775152,"prev_hashs":[],"hash":"b1abb1f1","signature":"xZg"})";

        auto no_dict = Compression::compress(data);
        QVERIFY(no_dict.has_value());

        auto with_dict = Compression::compress(data, dict);
        QVERIFY(with_dict.has_value());
        // Dictionary should improve ratio on small structured payloads
        QVERIFY(with_dict->size() <= no_dict->size());

        auto back = Compression::decompress(*with_dict, dict);
        QVERIFY(back.has_value());
        QCOMPARE(*back, data);
    }

    void compressionContextReuse() {
        std::string dict = R"({"section":"","type":0,"sender":"","receiver":""})";
        Compression::Context ctx(dict);

        for (int i = 0; i < 10; i++) {
            std::string data = R"({"section":")" + std::to_string(i) + R"(","type":0,"sender":"abc","receiver":"def"})";
            auto compressed = ctx.compress_frame(data);
            QVERIFY(compressed.has_value());

            auto back = ctx.decompress_frame(*compressed);
            QVERIFY(back.has_value());
            QCOMPARE(*back, data);
        }
    }

    void compressionEmptyInput() {
        auto compressed = Compression::compress("");
        QVERIFY(compressed.has_value());
        auto back = Compression::decompress(*compressed);
        QVERIFY(back.has_value());
        QCOMPARE(*back, std::string(""));
    }

    void compressionInvalidData() {
        std::string garbage = "this is not zstd data";
        auto result = Compression::decompress(garbage);
        QVERIFY(!result.has_value());
    }

    void packRoundTrip100() {
        std::filesystem::path p = std::filesystem::temp_directory_path() / "exc_pack_rt.pack";
        std::filesystem::remove(p);

        std::map<SectionId, std::string> input;
        for (int i = 0; i < 100; i++) {
            input.emplace(SectionId(i), R"({"section":")" + std::to_string(i)
                + R"(","type":0,"sender":"29c35573f5ff57b956de44a942f579bbc9843b13","amount":")"
                + std::to_string(i * 100) + R"("})");
        }

        auto w = Pack::write(p, 0, input);
        QVERIFY2(w.has_value(), "Pack::write failed");

        auto r = Pack::Reader::open(p);
        QVERIFY2(r.has_value(), "Pack::Reader::open failed");

        QCOMPARE(r->count(), static_cast<std::size_t>(100));
        QCOMPARE(r->first_section(), SectionId(0));
        QCOMPARE(r->last_section(), SectionId(99));

        for (int i = 0; i < 100; i++) {
            auto got = r->read(SectionId(i));
            QVERIFY2(got.has_value(), qPrintable(QString("read %1").arg(i)));
            QCOMPARE(*got, input[SectionId(i)]);
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
        QVERIFY(w.has_value());

        auto r = Pack::Reader::open(p);
        QVERIFY(r.has_value());

        QCOMPARE(r->id(), static_cast<Pack::PackId>(42));
        QCOMPARE(r->first_section(), SectionId(50));
        QCOMPARE(r->last_section(), SectionId(249));

        // Out of range
        QVERIFY(!r->read(SectionId(49)).has_value());
        QVERIFY(!r->read(SectionId(250)).has_value());

        // Random sample
        for (int id : {50, 100, 199, 249, 75}) {
            auto got = r->read(SectionId(id));
            QVERIFY(got.has_value());
            QCOMPARE(*got, std::string("payload-") + std::to_string(id));
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
        QVERIFY(w.has_value());

        auto r = Pack::Reader::open(p);
        QVERIFY(r.has_value());

        auto rng = r->read_range(SectionId(75), SectionId(125));
        QCOMPARE(rng.size(), static_cast<std::size_t>(51));
        for (std::size_t i = 0; i < rng.size(); i++) {
            int id = 75 + static_cast<int>(i);
            QCOMPARE(rng[i].first, SectionId(id));
            QCOMPARE(rng[i].second, std::string("item_") + std::to_string(id));
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
        QVERIFY(!w.has_value());
        QCOMPARE(w.error(), Pack::Error::NonConsecutiveSections);

        std::filesystem::remove(p);
    }

    void packCorruption() {
        std::filesystem::path p = std::filesystem::temp_directory_path() / "exc_pack_corrupt.pack";
        std::filesystem::remove(p);

        std::map<SectionId, std::string> input;
        input.emplace(SectionId(0), "hello");
        auto w = Pack::write(p, 0, input);
        QVERIFY(w.has_value());

        // Corrupt the file middle
        {
            std::fstream f(p, std::ios::binary | std::ios::in | std::ios::out);
            f.seekp(120);
            char bad = '\xFF';
            f.write(&bad, 1);
        }

        auto r = Pack::Reader::open(p);
        QVERIFY(!r.has_value());

        std::filesystem::remove(p);
    }

    void packEmpty() {
        std::filesystem::path p = std::filesystem::temp_directory_path() / "exc_pack_empty.pack";
        std::map<SectionId, std::string> input;
        auto w = Pack::write(p, 0, input);
        QVERIFY(!w.has_value());
        QCOMPARE(w.error(), Pack::Error::EmptyInput);
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
            QVERIFY2(w.has_value(), qPrintable(QString("pack %1").arg(p)));
        }

        // Read from each pack
        auto s0 = reg.read_section(SectionId(5));
        QVERIFY(s0.has_value());
        QCOMPARE(*s0, std::string("sec-5"));

        auto s1 = reg.read_section(SectionId(150));
        QVERIFY(s1.has_value());
        QCOMPARE(*s1, std::string("sec-150"));

        auto s2 = reg.read_section(SectionId(299));
        QVERIFY(s2.has_value());
        QCOMPARE(*s2, std::string("sec-299"));

        // Outside range
        QVERIFY(!reg.read_section(SectionId(300)).has_value());

        // Range that spans packs
        auto range = reg.read_sections(SectionId(95), SectionId(105));
        QCOMPARE(range.size(), static_cast<std::size_t>(11));
        QCOMPARE(range.front().first, SectionId(95));
        QCOMPARE(range.back().first, SectionId(105));

        // Coverage
        auto cov = reg.coverage();
        QVERIFY(cov.has_value());
        QCOMPARE(cov->first, SectionId(0));
        QCOMPARE(cov->last, SectionId(299));

        // Rescan preserves state
        reg.rescan();
        auto s3 = reg.read_section(SectionId(123));
        QVERIFY(s3.has_value());
        QCOMPARE(*s3, std::string("sec-123"));

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
            QVERIFY(w.has_value());
        }

        // Touch all, LRU should keep most recent 2 open
        for (int sid : {5, 15, 25, 35, 45, 5}) {
            auto s = reg.read_section(SectionId(sid));
            QVERIFY(s.has_value());
            QCOMPARE(*s, "v" + std::to_string(sid));
        }

        std::filesystem::remove_all(dir);
    }


    void createNetwork() {
        reset_qt_log_handler();
        QDir().mkdir("test-data");
        QDir::setCurrent("test-data");
        Utils::wipeDataFiles();
        node           = new ExtraChainNode;
        bool isCreated = node->createNewNetwork("login", "password", "Token", "1000", "#ffffff");
        QVERIFY(isCreated);
    }

    void chatJsonRemainsBackwardCompatible() {
        Actor<KeyPrivate> owner;
        owner.create(ActorType::User);

        Chat::Chat chat { .id       = "row-id",
                          .owner_id = owner.id(),
                          .file_id  = "chat-file" };
        auto json = Json::serialize_value(chat).as_object();
        json.erase("invite_pending");

        auto restored = Json::deserialize<Chat::Chat>(boost::json::serialize(json));
        QVERIFY(restored.has_value());
        QCOMPARE(restored->id, chat.id);
        QCOMPARE(restored->owner_id, chat.owner_id);
        QCOMPARE(restored->file_id, chat.file_id);
        QVERIFY(!restored->invite_pending);
    }

    void chatCreationPersistsBeforeSuccess() {
        QVERIFY(node != nullptr);

        auto activation = node->chat_manager()->activate();
        QVERIFY(activation.has_value());

        Actor<KeyPrivate> peer;
        peer.create(ActorType::User);

        auto created = node->chat_manager()->create_dialogue(peer.id());
        QVERIFY(created.has_value());
        QVERIFY(!created->id.empty());
        QVERIFY(!created->owner_id.is_zero());
        QVERIFY(!created->file_id.empty());

        auto stored = node->chat_manager()->read_chats();
        QVERIFY(stored.has_value());
        auto persisted = std::find_if(stored->cbegin(), stored->cend(), [&created](const auto &chat) {
            return chat.id == created->id && chat.owner_id == created->owner_id
                   && chat.file_id == created->file_id;
        });
        QVERIFY(persisted != stored->cend());
        QCOMPARE(persisted->invite_pending, created->invite_pending);
    }

    void targetedActorRefreshReportsNoActivePeers() {
        QVERIFY(node != nullptr);

        Actor<KeyPrivate> actor;
        actor.create(ActorType::User);

        QVERIFY(!node->dfs()->refresh_actors({ actor.id(), actor.id(), ActorId() }));
    }

    void vectorRowRebroadcastReportsNoActivePeers() {
        QVERIFY(node != nullptr);
        QVERIFY(!node->dfs()->rebroadcast_vector_row(ActorId(), "missing", "missing"));
    }

    void blocks() {
        //        Block a;
        //        Block b;
        //        Block pr;
        //        Transaction tr(node->getAccountController()->getCurrentActor().id(),
        //        BigNumber("ddddaaaa332232"),
        //                       BigNumber(124));
        //        Transaction tr1(node->getAccountController()->getCurrentActor().id(),
        //        BigNumber("322323dddaa"),
        //                        BigNumber(23));
        //        Transaction tr2(node->getAccountController()->getCurrentActor().id(),
        //        BigNumber("234234aaaa"),
        //                        BigNumber(45));
        //        Transaction tr3(node->getAccountController()->getCurrentActor().id(),
        //        BigNumber("23aaaaaaaaaa"),
        //                        BigNumber(4));
        //        QList<QByteArray> list;
        //        list << tr2.serialize() << tr3.serialize();
        //        QList<QByteArray> list2;
        //        list2 << tr.serialize() << tr1.serialize() << tr3.serialize();
        //        pr = Block(Serialization::serialize(list), Block());
        //        a = Block(Serialization::serialize(list), pr);
        //        b = Block(Serialization::serialize(list2));
        //        emit start1(a.serialize(), b.serialize());
    }
};

QTEST_MAIN(Test)
#include "test.moc"
