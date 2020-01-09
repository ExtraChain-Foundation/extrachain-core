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
    return Serialization::universalSerialize({ filePath.toLocal8Bit() }, DistFileSystem::fieldsSize);
}

void DistFileSystem::DfsRequest::deserialize(const QByteArray &serialized)
{
    filePath = Serialization::universalDeserialize(serialized, DistFileSystem::fieldsSize).takeFirst();
}
