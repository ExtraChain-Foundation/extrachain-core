#ifndef SIMPLE_MESSAGE_H
#define SIMPLE_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages {

struct BlockCount
{
    QByteArray request;

    BlockCount()
    {
        request = GET_BLOCK_COUNT_MESSAGE;
    }
    BlockCount(const QByteArray &serialized)
    {
        request = serialized;
    }

    const QByteArray serialize() const
    {
        return request;
    }
};

struct ActorCount
{
    QByteArray request;

    ActorCount()
    {
        request = GET_ACTOR_COUNT_MESSAGE;
    }
    ActorCount(const QByteArray &serialized)
    {
        request = serialized;
    }

    const QByteArray serialize() const
    {
        return request;
    }
};
}

#endif // SIMPLE_MESSAGE_H
