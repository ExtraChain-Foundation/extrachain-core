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

#include "blockchain/actor_id.h"
#include "utils/exc_utils.h"

enum class MessageType {
    Custom     = 0,
    NewActor   = 1,
    Actor      = 2,
    ActorCount = 3,
    ActorAll   = 4,
    Actors     = 15,

    BlockchainNewBlock     = 30,
    BlockchainSyncBlock    = 31,
    BlockchainTransaction  = 32,
    BlockchainCoinReward   = 35,
    BlockchainRequestBlock = 36,
    BlockchainSync         = 37,
    BlockchainLastSaved    = 38,

    BlockchainSyncLastInfo = 40, // last id, last hash

    BlockchainSyncBlocks = 49,

    DfsStoreFile = 50,
    // DfsSyncSearchFile   = 51, // parent for now
    // DfsSyncSearchResult = 52, // true or false
    DfsSyncDirs     = 53,
    DfsSyncDirsRows = 54,
    DfsSyncDirRows  = 55,

    DfsFileState = 56,
    // DfsFileWant      = 57,
    DfsStoreFragment = 58,
    DfsFileRequest   = 59,
    DfsFileFragment  = 60,
    // DfsFileThanks   = 61,
    DfsFileRemove = 62,

    DfsCollectionRequest   = 70,
    DfsCollectionContent   = 71,
    DfsCollectionHistory   = 72,
    DfsCollectionRowChange = 73,
    DfsVectorContent       = 74,
    DfsVectorAdd           = 75,
    DfsVectorRemove        = 76,
    DfsDictionaryContent   = 77,
    DfsDictionaryAdd       = 78,
    DfsDictionaryRemove    = 79,

    DfsTempSyncAll = 89,

    RequestDfsSize  = 90,
    ResponseDfsSize = 91,
    // DfsState = 92,
    RequestBlockCount  = 93,
    ResponseBlockCount = 94,

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

enum class SendMode {
    Neighbours,
    NeighboursRandom,
    OneNeighbourRandom,
    Broadcast,
    Except,
    Focused
};
MSGPACK_ADD_ENUM(SendMode)

struct MessageBody {
    SendMode                        send_type;
    MessageType                     message_type;
    MessageStatus                   status;
    std::string                     message_id;
    ActorId                         sender_id;
    ActorId                         init_sender_id;
    std::unordered_set<std::string> nodes_identifiers_to_ignore;
    std::unordered_set<std::string> nodes_identifiers_to_ignore_later;
    std::string                     data;

    std::string calculate_hash() const {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);

        auto send_type_val = std::to_underlying(send_type);
        blake3_hasher_update(&hasher, &send_type_val, sizeof(send_type_val));

        auto message_type_val = std::to_underlying(message_type);
        blake3_hasher_update(&hasher, &message_type_val, sizeof(message_type_val));

        auto status_val = std::to_underlying(status);
        blake3_hasher_update(&hasher, &status_val, sizeof(status_val));

        blake3_hasher_update(&hasher, message_id.data(), message_id.size());

        std::string init_sender_str = init_sender_id.to_string();
        blake3_hasher_update(&hasher, init_sender_str.data(), init_sender_str.size());

        blake3_hasher_update(&hasher, data.data(), data.size());

        uint8_t output[BLAKE3_OUT_LEN];
        blake3_hasher_finalize(&hasher, output, BLAKE3_OUT_LEN);

        return fmt::format("{:02x}", fmt::join(std::span(output, BLAKE3_OUT_LEN), ""));
    }

    std::string serialize() const {
        return MessagePack::serialize(*this);
    }

    MSGPACK_DEFINE(send_type,
                   message_type,
                   status,
                   message_id,
                   sender_id,
                   init_sender_id,
                   nodes_identifiers_to_ignore,
                   nodes_identifiers_to_ignore_later,
                   data)
};

struct NetworkPackageStorage {
    NetworkPackageStorage(const MessageBody& msg_body,
                          const std::string& prev_identifier,
                          const std::string& sign);

    const MessageBody msg_body;
    const std::string prev_identifier;
    const std::string sign;
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

inline MessageBody make_init_message(const std::string& data,
                                     SendMode           send_type,
                                     MessageType        type,
                                     MessageStatus      status,
                                     const ActorId&     sender,
                                     std::string        to_message_id) {
    if (!to_message_id.empty() && to_message_id.length() != 15) {
        eFatal("make message error: incorrect message id size");
    }

    std::string randomId = Utils::calculate_hash(std::to_string(QDateTime::currentSecsSinceEpoch())
                                                 + std::to_string(QRandomGenerator::global()->bounded(100000)))
                               .substr(0, 15); // temp

    MessageBody message = { .send_type      = send_type,
                            .message_type   = type,
                            .status         = status,
                            .message_id     = !to_message_id.empty() ? to_message_id : randomId,
                            .sender_id      = sender,
                            .init_sender_id = sender,
                            .data           = data };

    return message;
}

struct VPNMessage {
    std::string              initialSender;
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
    std::string              publicKey;
    std::string              uuid;
    std::vector<std::string> allIPsToSet;
    std::string              senderID;

    MSGPACK_DEFINE(initialSender,
                   vpnCommand,
                   vpnType,
                   resultChainIndex,
                   lockedChainIndex,
                   countryEndpoint,
                   proxyCounter,
                   lookingForNodeID,
                   networkIdentifiersToIgnore,
                   localIP,
                   publicIP,
                   publicKey,
                   uuid,
                   allIPsToSet,
                   senderID)
};
