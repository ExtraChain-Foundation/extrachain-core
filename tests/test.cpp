#include <QtTest/QtTest>
#include "managers/extrachain_node.h"
#include "managers/logs_manager.h"

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

    void createNetwork() {
        LogsManager::qtHandler();
        QDir().mkdir("test-data");
        QDir::setCurrent("test-data");
        Utils::wipeDataFiles();
        node = new ExtraChainNode;
        bool isCreated = node->createNewNetwork("email", "password", "Token", "1000", "#ffffff");
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
