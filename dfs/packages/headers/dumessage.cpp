#include "dumessage.h"

Message::DUMessage::DUMessage(const int &type, QObject *parent)
    : IDfs_Message(parent)
{
    this->type = static_cast<dfsMessageType>(type);
}

Message::DUMessage::DUMessage(const QByteArray &serialized, QObject *parent)
    : IDfs_Message(parent)
{
    QList<QByteArray> list = deserialize(serialized);
    type = static_cast<dfsMessageType>(list.takeFirst().toInt());
}

Message::DUMessage::~DUMessage()
{
}

const QByteArray Message::DUMessage::serialize() const
{
    return Serialization::universalSerialize(serializedParams());
}

const QList<QByteArray> Message::DUMessage::serializedParams() const
{
    return QList<QByteArray>();
}

const QList<QByteArray> Message::DUMessage::deserialize(const QByteArray &serialized)
{
    return Serialization::universalDeserialize(serialized);
}

const QByteArray Message::DUMessage::concatenate()
{
    return serializedParams().join();
}

const QByteArray Message::DUMessage::hash()
{
    return Utils::calcKeccak(concatenate());
}

int Message::DUMessage::getType() const
{
    return type;
}
