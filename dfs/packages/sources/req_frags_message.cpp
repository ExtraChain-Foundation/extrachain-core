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

#include "dfs/packages/headers/req_frags_message.h"

bool DistFileSystem::ReqFragsMessage::isEmpty() const
{
    if (filePath.isEmpty())
        return true;
    if (listFrag.isEmpty())
        return true;
    return false;
}

const QList<QByteArray> DistFileSystem::ReqFragsMessage::serializedParams() const
{
    QList<QByteArray> list;
    list << filePath.toUtf8() << listFrag;
    return list;
}

void DistFileSystem::ReqFragsMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

short DistFileSystem::ReqFragsMessage::getFieldsCount() const
{
    return ReqFragsMessage::FIELDS_COUNT;
}

QByteArray DistFileSystem::ReqFragsMessage::serialize() const
{
    return Serialization::serialize({ filePath.toLocal8Bit(), listFrag },
                                             DistFileSystem::fieldsSize);
}

void DistFileSystem::ReqFragsMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized, DistFileSystem::fieldsSize);
    filePath = l.takeFirst();
    listFrag = l.takeFirst();
}
