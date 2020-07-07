/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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
