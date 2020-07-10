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

#include "network/packages/service/connections_message.h"

void Messages::ConnectionsMessage::operator=(const QByteArray &serialized)
{
    deserialize(serialized);
}

void Messages::ConnectionsMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

void Messages::ConnectionsMessage::operator=(QList<QByteArray> &list)
{
    for (QByteArray d : list)
    {
        std::pair<std::string, int> p;
        QList<QByteArray> r = Serialization::deserialize(d, ConnectionsMessage::FIELD_SIZE);
        p.first = r.takeFirst().toStdString();
        p.second = r.takeFirst().toInt();
        hosts.push_back(p);
    }
}

bool Messages::ConnectionsMessage::isEmpty() const
{
    return hosts.empty();
}

QList<QByteArray> Messages::ConnectionsMessage::serializedParams() const
{
    QList<QByteArray> v;
    for (std::pair<std::string, int> p : hosts)
    {
        QByteArray r = Serialization::serialize(
            { QByteArray::fromStdString(p.first), QByteArray::number(p.second) },
            ConnectionsMessage::FIELD_SIZE);
        v << r;
    }
    return v;
}

short Messages::ConnectionsMessage::getFieldsCount() const
{
    return ConnectionsMessage::FIELDS_COUNT;
}

QByteArray Messages::ConnectionsMessage::serialize() const
{
    QList<QByteArray> list = serializedParams();
    return Serialization::serialize(list, FIELD_SIZE);
}

void Messages::ConnectionsMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> data = Serialization::deserialize(serialized, ConnectionsMessage::FIELD_SIZE);
    operator=(data);
}
