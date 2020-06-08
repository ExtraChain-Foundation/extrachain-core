#include "network/packages/service/get_all_actor_message.h"

using namespace Messages;

void GetAllActorMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool GetAllActorMessage::isEmpty() const
{
    return actorId.isEmpty();
}

short GetAllActorMessage::getFieldsCount() const
{
    return GetAllActorMessage::FIELDS_COUNT;
}

QByteArray GetAllActorMessage::serialize() const
{
    return Serialization::serialize({ actorId }, GetAllActorMessage::FIELD_SIZE);
}

void GetAllActorMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list =
        Serialization::deserialize({ serilaized }, GetAllActorMessage::FIELD_SIZE);
    if (list.size() > 0)
    {
        actorId = list.at(0);
    }
}
