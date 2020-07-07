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

#include "dfs/packages/headers/message_struct.h"

const QList<QByteArray> DistFileSystem::DfsMessage::serializedParams() const
{
    QList<QByteArray> list;
    list << dataHash << QByteArray::number(static_cast<long long>(pckgNumber)) << data << path.toUtf8();
    return list;
}

void DistFileSystem::DfsMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::DfsMessage::isEmpty() const
{
    return (dataHash.isEmpty()) || (data.isEmpty());
}

short DistFileSystem::DfsMessage::getFieldsCount() const
{
    return DfsMessage::FIELDS_COUNT;
}

QByteArray DistFileSystem::DfsMessage::serialize() const
{
    return Serialization::serialize(serializedParams());
}

void DistFileSystem::DfsMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized);
    if (l.size() == DfsMessage::FIELDS_COUNT)
    {
        dataHash = l.takeFirst();
        pckgNumber = l.takeFirst().toInt();
        data = l.takeFirst();
        path = l.takeFirst();
    }
}
