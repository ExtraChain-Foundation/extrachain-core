#ifndef MESSAGEBODY_H
#define MESSAGEBODY_H

#include "utils/exc_utils.h"

enum class MessageType
{
    Custom = 0,
    // ActorGet = 1,
    // ActorBody = 2,
    Actor = 1, // флаг response = true: handle actor, флаг response = false: std::string
    ActorCount = 3,
    ActorAll = 4
};
MSGPACK_ADD_ENUM(MessageType)

template <class T>
struct MessageBody {
    MessageType message_type;
    std::string message_id;
    // Config::Net::TypeSend type_send = Config::Net::TypeSend::All;
    T data;

    std::string serialize() const {
        return MessagePack::serialize(*this);
    }

    MSGPACK_DEFINE(message_type, message_id, data)
};

template <class T>
MessageBody<T> make_message(const T &t, MessageType type/*,
                            Config::Net::TypeSend type_send = Config::Net::TypeSend::All*/) {
    QByteArray randomId = Utils::calcKeccak(QByteArray::number(QDateTime::currentSecsSinceEpoch())
                                            + QByteArray::number(QRandomGenerator::global()->bounded(100000)))
                              .left(15);

    MessageBody<T> message = { .message_type = MessageType::ActorGetResponse,
                               .message_id = randomId.toStdString(),
                               // .type_send = type_send,
                               .data = t };
    return message;
}

#endif // MESSAGEBODY_H
