#pragma once

#include "network/message_body.h"

namespace Network {
    // Old nodes can obtain data and updates. They cannot change current network state.
    inline bool restricted_peer_message_allowed(MessageType type, MessageStatus status, bool incoming) {
        switch (type) {
        case MessageType::NewActor:
        case MessageType::Actor:
        case MessageType::Actors:
        case MessageType::ActorsHash:
            return true; // Actor payloads still require their own signature and the envelope signature.
        case MessageType::DagSections:
        case MessageType::DagLightData:
        case MessageType::DagIntervalHash:
        case MessageType::DagSyncLastInfo:
        case MessageType::DagFileSections:
        case MessageType::DagPackList:
        case MessageType::DfsSyncDirs:
        case MessageType::DfsSyncDirsRows:
        case MessageType::DfsSyncDirRows:
        case MessageType::DfsSyncDigest:
        case MessageType::DfsFileState:
        case MessageType::DfsCollectionRequest:
        case MessageType::DfsCollectionHistory:
        case MessageType::DfsVectorSyncRequest:
            return status == (incoming ? MessageStatus::Request : MessageStatus::Response);
        case MessageType::DagControlRangeRequest:
        case MessageType::DagPackRequest:
        case MessageType::RequestDfsSize:
            return incoming && status == MessageStatus::Request;
        case MessageType::DagControlRangeResponse:
        case MessageType::DagPackData:
        case MessageType::DfsSyncDigestReply:
        case MessageType::DfsFileExistNotification:
        case MessageType::DfsCollectionContent:
        case MessageType::DfsVectorContent:
        case MessageType::DfsDictionaryContent:
        case MessageType::DfsVectorSyncReply:
        case MessageType::ResponseDfsSize:
            return !incoming && status == MessageStatus::Response;
        case MessageType::DfsFileRequest:
            return incoming && (status == MessageStatus::NoStatus || status == MessageStatus::Request);
        case MessageType::DfsFileRequestContinueUpload:
            return status == (incoming ? MessageStatus::Request : MessageStatus::Response);
        case MessageType::DfsFileFragment:
            return !incoming && (status == MessageStatus::NoStatus || status == MessageStatus::Response);
        default:
            return false;
        }
    }
} // namespace Network
