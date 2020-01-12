#ifndef SIMPLE_MESSAGE_H
#define SIMPLE_MESSAGE_H
#include <QByteArray>
#include "headers/network/packages/service/message_types.h"

namespace Messages {

struct BlockCount
{
    QByteArray request;

    BlockCount()
    {
        request = QByteArray::number(Messages::GeneralRequest::GetBlockCount); /* GET_BLOCK_COUNT_MESSAGE;*/
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
        request = QByteArray::number(Messages::GeneralRequest::GetActorCount); /*GET_ACTOR_COUNT_MESSAGE;*/
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
