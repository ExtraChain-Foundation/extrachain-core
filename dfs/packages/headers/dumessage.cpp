#include "dumessage.h"

DFSMessage::DUMessage::DUMessage(QObject *parent)
    : IDfs_Message(parent)
{
    this->type = dfsMessageType::none;
}

DFSMessage::DUMessage::DUMessage(const int &type, QObject *parent)
    : IDfs_Message(parent)
{
    this->type = static_cast<dfsMessageType>(type);
}

DFSMessage::DUMessage::DUMessage(const QByteArray &serialized, QObject *parent)
    : IDfs_Message(parent)
{
    QList<QByteArray> list = deserialize(serialized);
    if (list.size() > 0)
        type = static_cast<dfsMessageType>(list.takeFirst().toInt());
}

const QByteArray DFSMessage::DUMessage::serialize() const
{
    return Serialization::universalSerialize(serializedParams(), Serialization::DFS_FIELD_SIZE);
}

const QList<QByteArray> DFSMessage::DUMessage::serializedParams() const
{
    return QList<QByteArray>();
}

const QList<QByteArray> DFSMessage::DUMessage::deserialize(const QByteArray &serialized)
{
    return Serialization::universalDeserialize(serialized, 8);
}

const QByteArray DFSMessage::DUMessage::concatenate()
{
    return serializedParams().join();
}

const QByteArray DFSMessage::DUMessage::hash()
{
    return Utils::calcKeccak(concatenate());
}

int DFSMessage::DUMessage::getType() const
{
    return type;
}

bool DFSMessage::DUMessage::isEmpty()
{
    if (this->type == dfsMessageType::none)
        return true;
    else
        return false;
}
