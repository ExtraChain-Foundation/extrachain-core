#ifndef GET_ACTOR_MESSAGE_H
#define GET_ACTOR_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {

class GetActorMessage
{
    short FIELDS_SIZE = 4;

private:
    BigNumber actorId;

public:
    GetActorMessage(const BigNumber &id);
    GetActorMessage(const QByteArray &serialized);
    ~GetActorMessage();

    const QByteArray serialize() const;
    void deserialize(const QByteArray &serilaized);
    BigNumber getActorId() const;
};
}

#endif // GET_ACTOR_MESSAGE_H
