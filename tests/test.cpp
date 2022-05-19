#include "managers/extrachain_node.h"
#include "managers/logs_manager.h"
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
    //    qDebug() << "Sign1:" << sign1;
    //    qDebug() << "Sign2:" << sign2;

    //    auto verify1 = key1.verify(message, sign1);
    //    auto verify2 = key2.verify(message, sign2);
    //    auto verify1Test = key1.verify(message, sign2);
    //    auto verify2Test = key2.verify(message, sign1);
    //    qDebug() << "Verify:" << verify1 << verify2 << verify1Test << verify2Test;

    //    auto encrypted = key1.encrypt(message, key2.publicKey());
    //    qDebug() << "Encrypt:" << encrypted;
    //    auto decrypted = key2.decrypt(encrypted, key1.publicKey());
    //    qDebug() << "Decrypt:" << decrypted;
    //    return 0;
    //    KeyPrivate key;
    //    QByteArray data = QByteArray("qweqe").repeated(30000);
    //    qDebug() << data.size();

    //    while (true) {
    //        QElapsedTimer timer;
    //        timer.start();
    //        for (int i = 0; i != 10000; i++)
    //            key.sign(data);
    //        qDebug() << timer.elapsed() << "ms";
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

//    qDebug() << b.toByteArray(10).toInt() << i;
//    qDebug() << (b - 5 == i - 5);

//    return 0;
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
