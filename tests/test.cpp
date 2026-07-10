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
#include "managers/account_controller.h"
#include "encryption/encryption_tools.h"
#include "utils/exc_logs.h"
#include <QtTest/QtTest>
#include <algorithm>
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
