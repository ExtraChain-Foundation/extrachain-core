#ifndef GET_ALL_ACTOR_MESSAGE_H
#define GET_ALL_ACTOR_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {

class GetAllActorMessage
{
    short FIELDS_SIZE = 4;

private:
    QList<QByteArray> actorId;
    BigNumber actor;

public:
    GetAllActorMessage(const BigNumber &id);
    GetAllActorMessage(const QByteArrayList &id);
    GetAllActorMessage(const QByteArray &serialized);
    ~GetAllActorMessage();

    const QByteArray serialize() const;
    void deserialize(const QByteArray &serilaized);
    BigNumber getActorId() const;
};
}
#endif // GET_ALL_ACTOR_MESSAGE_H
