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

#include <QDateTime>
#include <QList>

#include "utils/safeptr.h"

class ActorId;
enum class NetworkVPNType;

struct VPNConfigStorage {
    struct VPNHandhakeCache {
        bool is_sended = false;
        std::string uuid;
        // std::string              requesterIP;
        std::string              requesterNetworkIdentifier;
        std::string              requesterMessageID;
        std::string              requesterNodeID;
        std::string              nextNodeIP;
        std::string              nextNodeNetworkIdentifier;
        std::string              nextNodeID;
        NetworkVPNType           nextNodeIDType;
        int                      chainIndex;
        QDateTime                timestamp = QDateTime();
        std::string              localIPForSetup;
        std::string              proxyCounter;
        std::vector<std::string> allIPsToSet;
        std::string              requesterPublicKey;
        std::string              nextPublicKey;
    };

    struct VPNWorkers {
        std::string uuid;
        int         chainIndex;
        std::string requesterNodeID;
        std::string requesterPublicKey;
        std::string nextPublicKey;
        std::string nextNodeID;
        std::string nextNodeIP;
        std::string nextNodeNetworkIdentifier;
        qint64      lastRunExecuteTS = -1;
        qint64      lastUpdateRequsterTS;
        qint64      lastUpdateNextTS;
        qint64      lastSendedNextTS;

        qint64 lastWGTimestampRequester = -1;
        qint64 lastWGTimestampNext      = -1;
    };

    SafePtr<QList<VPNHandhakeCache>> vpnHandhakeCacheInProccess;

    std::atomic_bool vpnIsClient = false;
    // std::vector<std::string>                   vpnFileAddedFileId;
    SafePtr<std::map<std::string, VPNWorkers>> vpnUuidToVPNWorkers;
    std::optional<NetworkVPNType>              vpnConnectedType;
    std::string                                vpnLocalizationFileId;
};
