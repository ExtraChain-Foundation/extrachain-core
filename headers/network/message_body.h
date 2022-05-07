#ifndef MESSAGEBODY_H
#define MESSAGEBODY_H

#include <msgpack.hpp>

#include "datastorage/actor.h"
#include "utils/exc_utils.h"

enum class MessageType {
    Custom = 0,
    NewActor = 1,
    Actor = 2,
    ActorCount = 3,
    ActorAll = 4,

    DfsDirData = 50,
    DfsLastModified = 51,
    DfsAddFile = 52,
    DfsRequestFile = 53,
    DfsRequestFileSegment = 54,
    DfsRemoveFile = 55,
    DfsEditSegment = 56,
    DfsAddSegment = 57,
    DfsDeleteSegment = 58,
    DfsSendingFileDone = 59,
};
MSGPACK_ADD_ENUM(MessageType)

enum class MessageStatus {
    NoStatus,
    Request,
    Response
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

struct SocketIdentifier {
    std::string socketIdentifier;
    std::string messageId;
};

template <class T>
MessageBody<T> make_message(const T data, MessageType type, MessageStatus status, const ActorId &sender,
                            std::string to_message_id) {
    if (!to_message_id.empty() && to_message_id.length() != 15) {
        qFatal("make message error: incorrect message id size");
    }

    QByteArray randomId = Utils::calcHash(QByteArray::number(QDateTime::currentSecsSinceEpoch())
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
