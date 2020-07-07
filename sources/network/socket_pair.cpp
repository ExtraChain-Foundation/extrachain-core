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

#include "network/socket_pair.h"

SocketPair::SocketPair()
{

    iden = "0";
    ip = "0.0.0.0";
    port = 0;
}

SocketPair::SocketPair(const std::string &f, const quint16 &s)
{
    ip = f;
    port = s;

    iden = "0";
}

SocketPair::SocketPair(const SocketPair &v)
{
    ip = v.ip;
    port = v.port;

    iden = v.iden;
}

SocketPair::~SocketPair()
{
}

const QString SocketPair::serialize() const
{
    return QString::fromStdString(ip) + QString::number(port) + QString(iden);
}

const SocketPair SocketPair::operator=(const SocketPair &v)
{
    ip = v.ip;
    port = v.port;
    iden = v.iden;
    return *this;
}

bool SocketPair::operator==(const SocketPair &v) const
{
    return ((ip == v.ip) && (port == v.port) && (iden == v.iden));
}

BigNumber SocketPair::getId() const
{
    return iden;
}

void SocketPair::setId(const QByteArray &value)
{
    iden = value;
}

bool SocketPair::isEmpty() const
{
    if ((ip == "0.0.0.0") && (port == 0) && (iden == "0"))
        return true;
    else
        return false;
}

QDebug operator<<(QDebug d, const SocketPair &pair)
{
    d.noquote().nospace() << "Pair(ip: " << QString::fromStdString(pair.ip) << ", port: " << pair.port
                          << ", identificator: " << pair.iden << ")";
    return d;
}
