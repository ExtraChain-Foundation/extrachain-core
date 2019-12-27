#include "network/packages/service/get_actor_message.h"

using namespace Messages;

BigNumber GetActorMessage::getActorId() const
{
    return actorId;
}

GetActorMessage::GetActorMessage(const BigNumber &id)
{
    this->actorId = id;
}

GetActorMessage::GetActorMessage(const QByteArray &serialized)
{
    deserialize(serialized);
}

GetActorMessage::~GetActorMessage()
{
}

const QByteArray GetActorMessage::serialize() const
{
    return Serialization::universalSerialize({ actorId.toActorId() }, FIELDS_SIZE);
}

void GetActorMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::universalDeserialize(serilaized, FIELDS_SIZE);
    if (list.isEmpty())
        qDebug() << "get actor message error";
    this->actorId = BigNumber(list.at(0));
}
