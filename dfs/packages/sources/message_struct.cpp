#include "dfs/packages/headers/message_struct.h"

const QList<QByteArray> DistFileSystem::DfsMessage::serializedParams() const
{
    QList<QByteArray> list;
    list << dataHash << QByteArray::number(static_cast<long long>(pckgNumber)) << data << path.toUtf8();
    return list;
}

void DistFileSystem::DfsMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool DistFileSystem::DfsMessage::isEmpty() const
{
    return (dataHash.isEmpty()) || (data.isEmpty());
}

short DistFileSystem::DfsMessage::getFieldsCount() const
{
    return DfsMessage::FIELDS_COUNT;
}

QByteArray DistFileSystem::DfsMessage::serialize() const
{
    return Serialization::serialize(serializedParams());
}

void DistFileSystem::DfsMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::deserialize(serialized);
    if (l.size() == DfsMessage::FIELDS_COUNT)
    {
        dataHash = l.takeFirst();
        pckgNumber = l.takeFirst().toInt();
        data = l.takeFirst();
        path = l.takeFirst();
    }
}
