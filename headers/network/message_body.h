/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <QRandomGenerator>

#include <msgpack.hpp>

#include "blockchain/actor.h"
#include "utils/exc_utils.h"

enum class MessageType {
    Custom     = 0,
    NewActor   = 1,
    Actor      = 2,
    ActorCount = 3,
    ActorAll   = 4,

    BlockchainGenesisBlock = 30,
    BlockchainNewBlock     = 31,
    BlockchainTransaction  = 32,
    BlockchainCoinReward   = 35,
    BlockchainRequestBlock = 36,
    BlockchainSync         = 37,
    BlockchainLastSaved    = 38,
    BlockchainAnarchy      = 39,

    DfsDirData            = 50,
    DfsLastModified       = 51,
    DfsAddFile            = 52,
    DfsRequestFile        = 53,
    DfsRequestFileSegment = 54,
    DfsRemoveFile         = 55,
    DfsEditSegment        = 56,
    DfsAddSegment         = 57,
    DfsDeleteSegment      = 58,
    DfsSendingFileDone    = 59,
    DfsVerifyList         = 60,
    RequestDfsSize        = 61,
    ResponseDfsSize       = 62,
    // DfsState = 63,
    RequestBlockCount  = 64,
    ResponseBlockCount = 65,

    FragmentDataInfo      = 90,
    FragmentsDataListInfo = 91,

    NewListConnections    = 100,
    GetListConnections    = 101,
    ProcessNewConnections = 102,

    NewNodeConnected     = 110,
    SpreadNodeConnection = 111,
    RequestListNodes     = 112,

    ShareConnections = 113,

    Unknown = 250
};
MSGPACK_ADD_ENUM(MessageType)
// FORMAT_ENUM(MessageType)

enum class MessageStatus {
    NoStatus,
    Request,
    Response
};
MSGPACK_ADD_ENUM(MessageStatus)
// FORMAT_ENUM(MessageStatus)

struct MessageBody {
    MessageType   message_type;
    MessageStatus status;
    std::string   message_id;
    ActorId       sender_id;
    std::string   data;

    std::string serialize() const {
        return MessagePack::serialize(*this);
    }

    MSGPACK_DEFINE(message_type, status, message_id, sender_id, data)
};

struct SocketIdentifier {
    std::string socketIdentifier;
    std::string messageId;
};

struct CustomMessage {
    ActorId     owner;
    std::string data;

    MSGPACK_DEFINE(owner, data)
};

inline MessageBody make_message(const std::string &data,
                                MessageType        type,
                                MessageStatus      status,
                                const ActorId     &sender,
                                std::string        to_message_id) {
    if (!to_message_id.empty() && to_message_id.length() != 15) {
        eFatal("make message error: incorrect message id size");
    }

    std::string randomId = Utils::calculate_hash(std::to_string(QDateTime::currentSecsSinceEpoch())
                                                 + std::to_string(QRandomGenerator::global()->bounded(100000)))
                               .substr(0, 15); // temp

    MessageBody message = { .message_type = type,
                            .status       = status,
                            .message_id   = !to_message_id.empty() ? to_message_id : randomId,
                            .sender_id    = sender,
                            .data         = data };

    return message;
}

struct VPNMessage {
    int                      vpnCommand;
    int                      vpnType;
    int                      resultChainIndex;
    std::set<int>            lockedChainIndex;
    std::string              countryEndpoint;
    int                      proxyCounter;
    std::string              lookingForNodeID;
    std::set<std::string>    networkIdentifiersToIgnore;
    std::string              localIP;
    std::string              publicIP;
    std::string              publicKeyFile;
    std::string              uuid;
    std::vector<std::string> allIPsToSet;
    std::string              senderID;

    MSGPACK_DEFINE(vpnCommand,
                   vpnType,
                   resultChainIndex,
                   lockedChainIndex,
                   countryEndpoint,
                   proxyCounter,
                   lookingForNodeID,
                   networkIdentifiersToIgnore,
                   localIP,
                   publicIP,
                   publicKeyFile,
                   uuid,
                   allIPsToSet,
                   senderID)
};
