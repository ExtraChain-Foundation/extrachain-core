#include "dfs/packages/headers/hash_operations.h"

const QList<QByteArray> DistFileSystem::requestUpdate::serializedParams() const
{
    QList<QByteArray> list;
    list << filePath;
    return list;
}

void DistFileSystem::requestUpdate::operator=(QList<QByteArray> &list)
{
    if (list.size() == 1)
    {
        filePath = list.takeFirst();
    }
}

void DistFileSystem::requestUpdate::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::requestUpdate::isEmpty() const
{
    return filePath.isEmpty();
}

short DistFileSystem::requestUpdate::getFieldsCount() const
{
    return requestUpdate::FIELDS_COUNT;
}

QByteArray DistFileSystem::requestUpdate::serialize() const
{
    return Serialization::universalSerialize(serializedParams(), 2);
}

void DistFileSystem::requestUpdate::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::universalDeserialize(serialized, 2);
    operator=(l);
}
