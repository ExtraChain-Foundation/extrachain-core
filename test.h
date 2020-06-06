#ifndef TEST_H
#define TEST_H

#include "managers/node_manager.h"

class Test : public QObject
{
    Q_OBJECT
    Block a;
    Block b;
    Block pr;

public:
    Test(NodeManager *node, QObject *p = nullptr)
        : QObject(p)
    {
        Transaction tr(node->getAccController()->getCurrentActor().id(), BigNumber("ddddaaaa332232"),
                       BigNumber(124));
        Transaction tr1(node->getAccController()->getCurrentActor().id(), BigNumber("322323dddaa"),
                        BigNumber(23));
        Transaction tr2(node->getAccController()->getCurrentActor().id(), BigNumber("234234aaaa"),
                        BigNumber(45));
        Transaction tr3(node->getAccController()->getCurrentActor().id(), BigNumber("23aaaaaaaaaa"),
                        BigNumber(4));
        QList<QByteArray> list;
        list << tr2.serialize() << tr3.serialize();
        QList<QByteArray> list2;
        list2 << tr.serialize() << tr1.serialize() << tr3.serialize();
        pr = Block(Serialization::serialize(list), Block());
        a = Block(Serialization::serialize(list), pr);
        b = Block(Serialization::serialize(list2));
        emit start1(a.serialize(), b.serialize());
    }
signals:
    void start1(const QByteArray &a, const QByteArray &b);

public slots:
    void start()
    {
    }
};

#endif // TEST_H
