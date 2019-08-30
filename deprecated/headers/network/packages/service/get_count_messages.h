#ifndef SIMPLE_MESSAGE_H
#define SIMPLE_MESSAGE_H

#include "network/packages/base_message.h"

namespace Messages
{
    static const QByteArray GET_BLOCK_COUNT_MESSAGE = "getBlockCount";
    static const QByteArray GET_ACTOR_COUNT_MESSAGE = "getActorCount";

    static BaseMessage createGetBlockCountMessage()
    {
        return BaseMessage(GET_BLOCK_COUNT_MESSAGE);
    }

    static BaseMessage createGetActorCountMessage()
    {
        return BaseMessage(GET_ACTOR_COUNT_MESSAGE);
    }
}

#endif // SIMPLE_MESSAGE_H
