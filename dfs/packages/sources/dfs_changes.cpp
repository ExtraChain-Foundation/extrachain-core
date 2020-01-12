#include "dfs/packages/headers/dfs_changes.h"

const QList<QByteArray> DistFileSystem::DfsChanges::serializedParams() const
{
    QList<QByteArray> list;

    list << filePath.toUtf8() << Serialization::universalSerialize(data, DistFileSystem::fieldsSize) << range
         << QByteArray::number(changeType) << userId << signature << messHash;

    return list;
}

void DistFileSystem::DfsChanges::operator=(QList<QByteArray> &list)
{
    filePath = list.takeFirst();
    data = Serialization::universalDeserialize(list.takeFirst(), DistFileSystem::fieldsSize);
    range = list.takeFirst();
    changeType = list.takeFirst().toInt();
    userId = list.takeFirst();
    signature = list.takeFirst();
    messHash = list.takeFirst();
}

void DistFileSystem::DfsChanges::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::DfsChanges::isEmpty() const
{
    return filePath.isEmpty() || data.isEmpty() || range.isEmpty() || changeType == -1 || userId.isEmpty()
        || signature.isEmpty() || messHash.isEmpty();
}

short DistFileSystem::DfsChanges::getFieldsCount() const
{
    return DfsChanges::FIELDS_COUNT;
}

QByteArray DistFileSystem::DfsChanges::serialize() const
{
    return Serialization::universalSerialize(serializedParams(), DistFileSystem::fieldsSize);
}

void DistFileSystem::DfsChanges::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::universalDeserialize(serialized, DistFileSystem::fieldsSize);
    operator=(l);
}
