#ifndef MESSAGEBODY_H
#define MESSAGEBODY_H

#include "datastorage/actor.h"
#include "utils/exc_utils.h"

enum class MessageType
{
    Custom = 0,
    // ActorGet = 1,
    // ActorBody = 2,
    Actor = 1, // флаг response = true: handle actor, флаг response = false: std::string
    ActorCount = 2,
    ActorAll = 4,

    DfsAddFile = 50,
    DfsRequestFileSegment = 51,
    DfsRemoveFile = 52,
    DfsEditSegment = 53,
    DfsAddSegment = 54,
    DfsDeleteSegment = 55
};
MSGPACK_ADD_ENUM(MessageType)

enum class MessageStatus
{
    NoStatus,
    Request,
    Response,
    InOne
};
MSGPACK_ADD_ENUM(MessageStatus)

template <class T>
struct MessageBody {
    MessageType message_type;
    MessageStatus status;
    std::string message_id;
    ActorId sender_id;
    T data;

    std::string serialize() const {
        return MessagePack::serialize(*this);
    }

    MSGPACK_DEFINE(message_type, status, message_id, sender_id, data)
};

template <class T>
MessageBody<T> make_message(const T data, MessageType type, MessageStatus status, const ActorId &sender,
                            std::string to_message_id) {
    QByteArray randomId = Utils::calcKeccak(QByteArray::number(QDateTime::currentSecsSinceEpoch())
                                            + QByteArray::number(QRandomGenerator::global()->bounded(100000)))
                              .left(15); // temp

    MessageBody<T> message = { .message_type = type,
                               .status = status,
                               .message_id = !to_message_id.empty() ? to_message_id : randomId.toStdString(),
                               .sender_id = sender.toStdString(),
                               .data = data };

    return message;
}

#endif // MESSAGEBODY_H
