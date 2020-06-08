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
