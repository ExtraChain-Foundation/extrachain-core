#ifndef BASEMESSAGE_H
#define BASEMESSAGE_H

#include "utils/exc_utils.h"

enum class MessageType
{
    ActorGet = 1,
    ActorGetResponse = 2
};
MSGPACK_ADD_ENUM(MessageType)

template <class T>
struct MessageBody {
    MessageType message_type;
    std::string message_id;
    T data;

    AUTO_SERIALIZE(message_type, message_id, data)
};

#endif // BASEMESSAGE_H
