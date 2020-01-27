#include "dfs/packages/headers/hash_operations.h"

const QList<QByteArray> DistFileSystem::requestLast::serializedParams() const
{
    QList<QByteArray> list;
    list << id;
    return list;
}

void DistFileSystem::requestLast::operator=(QList<QByteArray> &list)
{
    if (list.size() == 1)
    {
        id = list.takeFirst();
    }
}

void DistFileSystem::requestLast::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::requestLast::isEmpty() const
{
    return id.isEmpty();
}

short DistFileSystem::requestLast::getFieldsCount() const
{
    return requestLast::FIELDS_COUNT;
}

QByteArray DistFileSystem::requestLast::serialize() const
{
    return Serialization::universalSerialize(serializedParams(), 2);
}

void DistFileSystem::requestLast::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::universalDeserialize(serialized, 2);
    operator=(l);
}

const QList<QByteArray> DistFileSystem::responseLast::serializedParams() const
{
    QList<QByteArray> list;
    list << pHash << cHash;
    return list;
}

void DistFileSystem::responseLast::operator=(QList<QByteArray> &list)
{
    if (list.size() == 2)
    {
        pHash = list.takeFirst();
        cHash = list.takeFirst();
    }
}

void DistFileSystem::responseLast::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::responseLast::isEmpty() const
{
    return pHash.isEmpty() || cHash.isEmpty();
}

short DistFileSystem::responseLast::getFieldsCount() const
{
    return responseLast::FIELDS_COUNT;
}

QByteArray DistFileSystem::responseLast::serialize() const
{
    return Serialization::universalSerialize(serializedParams(), 2);
}

void DistFileSystem::responseLast::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::universalDeserialize(serialized, 2);
    operator=(l);
}
