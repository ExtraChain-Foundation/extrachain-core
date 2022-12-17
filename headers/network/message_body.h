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
    DfsVerifyList = 60,
    RequestDfsSize = 61,
    ResponseDfsSize = 62,
    DfsState = 63,

    BlockchainGenesisBlock = 80,
    BlockchainNewBlock = 81,
    BlockchainTransaction = 82,
    BlockchainCopyScript = 83,
    BlockchainDataMiningRewardTransaction = 84,
    BlockchainCoinReward = 85,

    FragmentDataInfo = 90,
    FragmentsDataListInfo = 91
};
MSGPACK_ADD_ENUM(MessageType)
FORMAT_ENUM(MessageType)

enum class MessageStatus {
    NoStatus,
    Request,
    Response
};
MSGPACK_ADD_ENUM(MessageStatus)
FORMAT_ENUM(MessageStatus)

struct MessageBody {
    MessageType message_type;
    MessageStatus status;
    std::string message_id;
    ActorId sender_id;
    std::string data;

    std::string serialize() const {
        return MessagePack::serialize(*this);
    }

    MSGPACK_DEFINE(message_type, status, message_id, sender_id, data)
};

struct SocketIdentifier {
    std::string socketIdentifier;
    std::string messageId;
};

inline MessageBody make_message(const std::string &data, MessageType type, MessageStatus status,
                                const ActorId &sender, std::string to_message_id) {
    if (!to_message_id.empty() && to_message_id.length() != 15) {
        qFatal("make message error: incorrect message id size");
    }

    std::string randomId = Utils::calcHash(std::to_string(QDateTime::currentSecsSinceEpoch())
                                           + std::to_string(QRandomGenerator::global()->bounded(100000)))
                               .substr(0, 15); // temp

    MessageBody message = { .message_type = type,
                            .status = status,
                            .message_id = !to_message_id.empty() ? to_message_id : randomId,
                            .sender_id = sender.toStdString(),
                            .data = data };

    return message;
}

#endif // MESSAGEBODY_H
