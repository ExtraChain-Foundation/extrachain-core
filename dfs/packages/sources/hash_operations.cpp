#include "dfs/packages/headers/hash_operations.h"

const QList<QByteArray> DistFileSystem::requestLast::serializedParams() const
{
    QList<QByteArray> list;
    list << actorId;
    return list;
}

void DistFileSystem::requestLast::operator=(QList<QByteArray> &list)
{
    if (list.size() == 1)
    {
        actorId = list.takeFirst();
    }
}

void DistFileSystem::requestLast::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::requestLast::isEmpty() const
{
    return actorId.isEmpty();
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
    list << actorId << pHash << cHash;
    return list;
}

void DistFileSystem::responseLast::operator=(QList<QByteArray> &list)
{
    if (list.size() == FIELDS_COUNT)
    {
        actorId = list.takeFirst();
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
    return actorId.isEmpty() || pHash.isEmpty() || cHash.isEmpty();
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

const QList<QByteArray> DistFileSystem::CardFileChange::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(key) << actorId << fileId << prevId << nextId << QByteArray::number(type)
         << sign;
    return list;
}

void DistFileSystem::CardFileChange::operator=(QList<QByteArray> &list)
{
    if (list.size() == FIELDS_COUNT)
    {
        key = list.takeFirst().toInt();
        actorId = list.takeFirst();
        fileId = list.takeFirst();
        prevId = list.takeFirst();
        nextId = list.takeFirst();
        type = list.takeFirst().toInt();
        sign = list.takeFirst();
    }
}

void DistFileSystem::CardFileChange::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::CardFileChange::isEmpty() const
{
    return key == -1 || actorId.isEmpty() || fileId.isEmpty() || prevId.isEmpty() || nextId.isEmpty()
        || type == -1 || sign.isEmpty();
}

short DistFileSystem::CardFileChange::getFieldsCount() const
{
    return CardFileChange::FIELDS_COUNT;
}

QByteArray DistFileSystem::CardFileChange::serialize() const
{
    return Serialization::universalSerialize(serializedParams(), 2);
}

void DistFileSystem::CardFileChange::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::universalDeserialize(serialized, 2);
    operator=(l);
}
