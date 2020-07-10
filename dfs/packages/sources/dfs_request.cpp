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

#include "dfs/packages/headers/dfs_request.h"

bool DistFileSystem::DfsRequest::isEmpty() const
{
    return filePath.isEmpty();
}

const QList<QByteArray> DistFileSystem::DfsRequest::serializedParams() const
{
    QList<QByteArray> list;
    list << filePath.toUtf8();
    return list;
}

void DistFileSystem::DfsRequest::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

short DistFileSystem::DfsRequest::getFieldsCount() const
{
    return DfsRequest::FIELDS_COUNT;
}

QByteArray DistFileSystem::DfsRequest::serialize() const
{
    return Serialization::serialize({ filePath.toLocal8Bit() }, DistFileSystem::fieldsSize);
}

void DistFileSystem::DfsRequest::deserialize(const QByteArray &serialized)
{
    filePath = Serialization::deserialize(serialized, DistFileSystem::fieldsSize).takeFirst();
}

bool DistFileSystem::DfsRequestFinished::isEmpty() const
{
    return filePath.isEmpty();
}

const QList<QByteArray> DistFileSystem::DfsRequestFinished::serializedParams() const
{
    return {};
}

void DistFileSystem::DfsRequestFinished::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

short DistFileSystem::DfsRequestFinished::getFieldsCount() const
{
    return DfsRequestFinished::FIELDS_COUNT;
}

QByteArray DistFileSystem::DfsRequestFinished::serialize() const
{
    return "{\"filePath\":\"" + filePath.toLatin1() + "\"}";
}

void DistFileSystem::DfsRequestFinished::deserialize(const QByteArray &serialized)
{
    QJsonDocument json = QJsonDocument::fromJson(serialized);
    filePath = json["filePath"].toString();
}
