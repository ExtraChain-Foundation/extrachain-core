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

#ifndef SOCKET_PAIR_H
#define SOCKET_PAIR_H

#include "utils/bignumber.h"

class SocketPair
{
public:
    std::string ip;
    quint16 port;
    QByteArray iden;
    SocketPair();
    SocketPair(const std::string &f, const quint16 &s);
    SocketPair(const SocketPair &v);
    const QString serialize() const;
    const SocketPair operator=(const SocketPair &v);
    bool operator==(const SocketPair &v) const;
    ~SocketPair();
    BigNumber getId() const;
    void setId(const QByteArray &value);
    bool isEmpty() const;
};

inline uint qHash(const SocketPair &v)
{
    return qHash(v.serialize());
}

QDebug operator<<(QDebug d, const SocketPair &pair);

#endif // SOCKET_PAIR_H
