#ifndef MESSAGEBODY_H
#define MESSAGEBODY_H

#include "utils/exc_utils.h"

enum class MessageType
{
    Custom = 0,
    // ActorGet = 1,
    // ActorBody = 2,
    Actor = 1, // флаг response = true: handle actor, флаг response = false: std::string
    ActorCount = 2,
    ActorAll = 4
};
MSGPACK_ADD_ENUM(MessageType)

enum class MessageStatus
{
    Request,
    Response
};
MSGPACK_ADD_ENUM(MessageStatus)

template <class T>
struct MessageBody {
    MessageType message_type;
    bool is_response;
    std::string message_id;
    std::string sender_id;
    T data;

    std::string serialize() const {
        return MessagePack::serialize(*this);
    }

    MSGPACK_DEFINE(message_type, is_response, message_id, data)
};

template <class T>
MessageBody<T> make_message(const T data, MessageType type, MessageStatus status, const std::string &sender,
                            std::string to_message_id) {
    QByteArray randomId = Utils::calcKeccak(QByteArray::number(QDateTime::currentSecsSinceEpoch())
                                            + QByteArray::number(QRandomGenerator::global()->bounded(100000)))
                              .left(15); // temp

    MessageBody<T> message = { .message_type = type,
                               .is_response = status == MessageStatus::Response,
                               .message_id = !to_message_id.empty() ? to_message_id : randomId.toStdString(),
                               .sender_id = sender,
                               .data = data };

    return message;
}

#endif // MESSAGEBODY_H
