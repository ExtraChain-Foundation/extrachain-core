#include "network/packages/service/get_actor_message.h"

using namespace Messages;

void GetActorMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool GetActorMessage::isEmpty() const
{
    if (actorId.isEmpty())
        return true;
    else
        return false;
}

short GetActorMessage::getFieldsCount() const
{
    return FIELDS_COUNT;
}

QByteArray GetActorMessage::serialize() const
{
    return Serialization::serialize({ actorId.toActorId() }, GetActorMessage::FIELD_SIZE);
}

void GetActorMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::deserialize(serilaized, GetActorMessage::FIELD_SIZE);
    if (list.isEmpty())
        qDebug() << "get actor message error";
    this->actorId = BigNumber(list.at(0));
}
