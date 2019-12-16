#include "network/packages/service/get_all_actor_message.h"

using namespace Messages;

BigNumber GetAllActorMessage::getActorId() const
{
    return actor;
}

GetAllActorMessage::GetAllActorMessage(const BigNumber &id)
{
    actor = id;
}

GetAllActorMessage::GetAllActorMessage(const QByteArrayList &id)
{
    this->actorId = id;
}

GetAllActorMessage::GetAllActorMessage(const QByteArray &serialized)
{
    deserialize(serialized);
}

GetAllActorMessage::~GetAllActorMessage()
{
}

const QByteArray GetAllActorMessage::serialize() const
{
    return Serialization::universalSerialize({ actorId }, FIELDS_SIZE);
}

void GetAllActorMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::universalDeserialize(serilaized, FIELDS_SIZE);
    this->actorId = list;
}
