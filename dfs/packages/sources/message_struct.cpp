#include "dfs/packages/headers/message_struct.h"

const QList<QByteArray> DistFileSystem::DfsMessage::serializedParams() const
{
    QList<QByteArray> list;
    list << dataHash << QByteArray::number(static_cast<long long>(pckgNumber)) << data;
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
    return Serialization::universalSerialize(serializedParams());
}

void DistFileSystem::DfsMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> l = Serialization::universalDeserialize(serialized);
    if (l.size() == 3)
    {
        dataHash = l.takeFirst();
        pckgNumber = l.takeFirst().toInt();
        data = l.takeFirst();
    }
}
