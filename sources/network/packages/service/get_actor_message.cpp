#include "network/packages/service/get_actor_message.h"

using namespace Messages;

Messages::GetActorMessage::GetActorMessage(const BigNumber &actorId)
    : BaseMessage(GET_ACTOR_MESSAGE)
    , actorId(actorId)
{
}

Messages::GetActorMessage::GetActorMessage(const QByteArray &serialized)
{
    QList<QByteArray> msgElemList = BaseMessage::deserializeToList(serialized);
//    BaseMessage::initFields(msgElemList);
    initFields(msgElemList);
}

short GetActorMessage::getFieldsCount() const
{
    return BaseMessage::getFieldsCount() + 1;
}

void GetActorMessage::initFields(QList<QByteArray> &list)
{
    BaseMessage::initFields(list);
    actorId = BigNumber(list.takeFirst());
}

QList<QByteArray> GetActorMessage::serializedParams() const
{
    QList<QByteArray> l = BaseMessage::serializedParams();
    l << actorId.serialize();
    return l;
}

BigNumber GetActorMessage::getActorId() const
{
    return actorId;
}
