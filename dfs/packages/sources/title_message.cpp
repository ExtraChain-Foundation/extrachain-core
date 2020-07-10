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

#include "dfs/packages/headers/title_message.h"

bool DistFileSystem::titleMessage::isEmpty() const
{
    if (filePath.isEmpty())
        return true;
    if (pckgsAmount == 0)
        return true;
    if (fileSize == 0)
        return true;
    if (dataHash.isEmpty())
        return true;
    // if (prevId.isEmpty())
    //    return true;
    return false;
}

void DistFileSystem::titleMessage::calcHash()
{
    QFile file(filePath);
    if (file.exists())
    {
        file.open(QIODevice::ReadOnly);
        fileSize = file.size();
        while (file.pos() + dataSize < file.size())
        {
            pckgsAmount++;
            QByteArray sgmHash = Utils::calcKeccak(file.read(dataSize));
            dataHash = Utils::calcKeccak(dataHash + sgmHash);
        }
        pckgsAmount++;
        QByteArray sgmHash = Utils::calcKeccak(file.read(file.size() - file.pos()));
        dataHash = Utils::calcKeccak(dataHash + sgmHash);
        file.close();
    }
}

const QList<QByteArray> DistFileSystem::titleMessage::serializedParams() const
{
    QList<QByteArray> list;
    list << filePath.toUtf8() << QByteArray::number(static_cast<long long>(pckgsAmount))
         << QByteArray::number(fileSize) << dataHash << QByteArray::number(f_type) << prevId;
    return list;
}

void DistFileSystem::titleMessage::operator=(DistFileSystem::TitleMessage title)
{
    filePath = title.filePath;
    pckgsAmount = title.pckgsAmount;
    fileSize = title.fileSize;
    f_type = title.f_type;
    dataHash = title.dataHash;
    prevId = title.prevId;
}

void DistFileSystem::titleMessage::operator=(const QByteArray &serialized)
{
    deserialize(serialized);
}

void DistFileSystem::titleMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

short DistFileSystem::titleMessage::getFieldsCount() const
{
    return titleMessage::FIELDS_COUNT;
}

QByteArray DistFileSystem::titleMessage::serialize() const
{
    return Serialization::serialize(serializedParams(), DistFileSystem::fieldsSize);
}

void DistFileSystem::titleMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized);

    if (l.size() != FIELDS_COUNT)
        return;

    filePath = l.takeFirst();
    pckgsAmount = l.takeFirst().toULong();
    fileSize = l.takeFirst().toLongLong();
    dataHash = l.takeFirst();
    f_type = l.takeFirst().toUInt();
    prevId = l.takeFirst();
}
