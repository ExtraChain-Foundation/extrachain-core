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

#include "dfs/packages/headers/dfs_changes.h"

const QList<QByteArray> DistFileSystem::DfsChanges::serializedParams() const
{
    QList<QByteArray> list;

    list << filePath.toUtf8() << Serialization::serialize(data, DistFileSystem::fieldsSize) << range
         << QByteArray::number(changeType) << userId << sign << messHash << prevHash
         << QByteArray::number(fileVersion);

    return list;
}

void DistFileSystem::DfsChanges::operator=(QList<QByteArray> &list)
{
    filePath = list.takeFirst();
    data = Serialization::deserialize(list.takeFirst(), DistFileSystem::fieldsSize);
    range = list.takeFirst();
    changeType = list.takeFirst().toInt();
    userId = list.takeFirst();
    sign = list.takeFirst();
    messHash = list.takeFirst();
    prevHash = list.takeFirst();
    fileVersion = list.takeFirst().toInt();
}

void DistFileSystem::DfsChanges::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::DfsChanges::isEmpty() const
{
    return filePath.isEmpty() || data.isEmpty() || range.isEmpty() || changeType == -1 || userId.isEmpty()
        || sign.isEmpty() || messHash.isEmpty() || fileVersion == -1;
}

short DistFileSystem::DfsChanges::getFieldsCount() const
{
    return DfsChanges::FIELDS_COUNT;
}

QByteArray DistFileSystem::DfsChanges::serialize() const
{
    return Serialization::serialize(serializedParams(), DistFileSystem::fieldsSize);
}

void DistFileSystem::DfsChanges::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized, DistFileSystem::fieldsSize);
    operator=(l);
}

QByteArray DistFileSystem::DfsChanges::prepareSign()
{
    QList<QByteArray> list;
    list << filePath.toUtf8() << Serialization::serialize(data, DistFileSystem::fieldsSize) << range
         << QByteArray::number(changeType) << userId << messHash << prevHash
         << QByteArray::number(fileVersion);
    // qDebug() << "prepareSign" << list;
    QByteArray keccak =
        Utils::calcKeccak(Serialization::serialize(list, DistFileSystem::fieldsSize));
    return keccak;
}
