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

#include "managers/extrachain_node.h"
#include "dfs/fragments/merkle.h"
#include "dfs/fragments/fragment_storage.h"
#include "utils/exc_logs.h"
#include <QtTest/QtTest>

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

    void bigNumberTest() {
        //    BigNumber b(1);
        //    b++;
        //    b--;
        //    --b;
        //    ++b;
        //    b = b + 7;
        //    b = b - 4;
        //    b += 4;
        //    b -= 6;
        //    b *= 4;
        //    b = b * 4;
        //    b /= 2;
        //    b = b / 2;

        //    int i(1);
        //    i++;
        //    i--;
        //    --i;
        //    ++i;
        //    i = i + 7;
        //    i = i - 4;
        //    i += 4;
        //    i -= 6;
        //    i *= 4;
        //    i = i * 4;
        //    i /= 2;
        //    i = i / 2;

        //    eLog("{} {}", b.toByteArray(10).toInt(), i);
        //    eLog("{}", (b - 5 == i - 5));

        //    return 0;
    }

    void merkle_hash_leaf_deterministic() {
        std::vector<uint8_t> data(1024, 0xAB);
        auto h1 = Dfs::Fragments::hash_leaf(data.data(), data.size());
        auto h2 = Dfs::Fragments::hash_leaf(data.data(), data.size());
        QCOMPARE(h1, h2);
    }

    void merkle_hash_leaf_different_data() {
        std::vector<uint8_t> a(1024, 0xAB);
        std::vector<uint8_t> b(1024, 0xCD);
        QVERIFY(Dfs::Fragments::hash_leaf(a.data(), a.size())
                != Dfs::Fragments::hash_leaf(b.data(), b.size()));
    }

    void merkle_hash_node_order_matters() {
        std::vector<uint8_t> a(512, 0x01), b(512, 0x02);
        auto ha = Dfs::Fragments::hash_leaf(a.data(), a.size());
        auto hb = Dfs::Fragments::hash_leaf(b.data(), b.size());
        QVERIFY(Dfs::Fragments::hash_node(ha, hb) != Dfs::Fragments::hash_node(hb, ha));
    }

    void merkle_single_leaf_is_root() {
        std::vector<uint8_t> data(256, 0xFF);
        auto leaf = Dfs::Fragments::hash_leaf(data.data(), data.size());
        QCOMPARE(Dfs::Fragments::compute_root({ leaf }), leaf);
    }

    void merkle_two_leaves() {
        std::vector<uint8_t> a(256, 0x01), b(256, 0x02);
        auto la = Dfs::Fragments::hash_leaf(a.data(), a.size());
        auto lb = Dfs::Fragments::hash_leaf(b.data(), b.size());
        QCOMPARE(Dfs::Fragments::compute_root({ la, lb }), Dfs::Fragments::hash_node(la, lb));
    }

    void merkle_three_leaves_odd_duplication() {
        std::vector<uint8_t> a(256, 0x01), b(256, 0x02), c(256, 0x03);
        auto la = Dfs::Fragments::hash_leaf(a.data(), a.size());
        auto lb = Dfs::Fragments::hash_leaf(b.data(), b.size());
        auto lc = Dfs::Fragments::hash_leaf(c.data(), c.size());
        auto expected = Dfs::Fragments::hash_node(
            Dfs::Fragments::hash_node(la, lb),
            Dfs::Fragments::hash_node(lc, lc));
        QCOMPARE(Dfs::Fragments::compute_root({ la, lb, lc }), expected);
    }

    void merkle_empty_leaves() {
        Dfs::Fragments::Hash32 zero {};
        QCOMPARE(Dfs::Fragments::compute_root({}), zero);
    }

    void merkle_verify_leaf_correct() {
        std::vector<uint8_t> data(512000, 0x42);
        auto expected = Dfs::Fragments::hash_leaf(data.data(), data.size());
        QVERIFY(Dfs::Fragments::verify_leaf(expected, data.data(), data.size()));
    }

    void merkle_verify_leaf_corrupted() {
        std::vector<uint8_t> data(512000, 0x42);
        auto expected = Dfs::Fragments::hash_leaf(data.data(), data.size());
        data[0] = 0x00;
        QVERIFY(!Dfs::Fragments::verify_leaf(expected, data.data(), data.size()));
    }

    void fragments_hex_roundtrip() {
        std::vector<uint8_t> data(100, 0xDE);
        auto h = Dfs::Fragments::hash_leaf(data.data(), data.size());
        auto hex = Dfs::Fragments::to_hex(h);
        QCOMPARE(Dfs::Fragments::from_hex(hex), h);
        QCOMPARE(hex.size(), size_t(64));
    }

    void fragments_hash_prefix() {
        QVERIFY(Dfs::Fragments::is_fragment_hash("fg:abcdef"));
        QVERIFY(!Dfs::Fragments::is_fragment_hash("abcdef"));
        QCOMPARE(Dfs::Fragments::parse_merkle_root_hex("fg:abcdef"), std::string("abcdef"));
        QVERIFY(Dfs::Fragments::parse_merkle_root_hex("abcdef").empty());
    }

    void fragments_file_write_read_roundtrip() {
        using namespace Dfs::Fragments;

        std::vector<Hash32> leaves;
        for (int i = 0; i < 10; i++) {
            std::vector<uint8_t> d(512, static_cast<uint8_t>(i));
            leaves.push_back(hash_leaf(d.data(), d.size()));
        }
        auto root = compute_root(leaves);

        FragmentsFile ff {
            .version = STORAGE_VERSION,
            .fragment_size = MERKLE_LEAF_SIZE,
            .fragment_count = static_cast<uint32_t>(leaves.size()),
            .merkle_root = root,
            .leaves = leaves,
        };

        auto path = std::filesystem::temp_directory_path() / "test_merkle.fragments";
        auto wr = write(path, ff);
        QVERIFY(wr.has_value());

        auto rd = read(path);
        QVERIFY(rd.has_value());
        QCOMPARE(rd->merkle_root, root);
        QCOMPARE(rd->leaves.size(), leaves.size());
        QCOMPARE(rd->fragment_count, static_cast<uint32_t>(leaves.size()));
        QCOMPARE(rd->version, STORAGE_VERSION);

        for (size_t i = 0; i < leaves.size(); i++) {
            QCOMPARE(rd->leaves[i], leaves[i]);
        }

        std::filesystem::remove(path);
    }

    void merkle_large_tree() {
        std::vector<Dfs::Fragments::Hash32> leaves;
        for (int i = 0; i < 10000; i++) {
            std::vector<uint8_t> d(32, static_cast<uint8_t>(i % 256));
            leaves.push_back(Dfs::Fragments::hash_leaf(d.data(), d.size()));
        }
        auto r1 = Dfs::Fragments::compute_root(leaves);
        auto r2 = Dfs::Fragments::compute_root(leaves);
        QCOMPARE(r1, r2);
        QVERIFY(r1 != Dfs::Fragments::Hash32 {});
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
