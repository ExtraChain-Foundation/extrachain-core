#ifndef VPN_TYPES_H
#define VPN_TYPES_H

#include "utils/safeptr.h"

class ActorId;
enum class NetworkVPNType;

struct VPNConfigStorage {
    struct VPNHandhakeCache {
        std::string              uuid;
        std::string              requesterIP;
        std::string              requesterMessageID;
        std::string              requesterNodeID;
        std::string              nextNodeIP;
        std::string              nextNodeID;
        NetworkVPNType           nextNodeIDType;
        int                      chainIndex;
        QDateTime                timestamp = QDateTime();
        std::string              localIPForSetup;
        std::string              proxyCounter;
        std::vector<std::string> allIPsToSet;
        std::string              requesterPublicKeyFile;
        std::string              nextPublicKeyFile;
    };

    struct VPNWorkers {
        std::string uuid;
        int         chainIndex;
        std::string requesterNodeID;
        std::string nextNodeID;
        std::string nextNodeIP;
        qint64      lastUpdateRequsterTS;
        qint64      lastUpdateNextTS;
        qint64      lastSendedNextTS;
    };

    SafePtr<QList<VPNHandhakeCache>> vpnHandhakeCacheInProccess;

    std::atomic_bool                           vpnIsClient = false;
    std::vector<std::string>                   vpnFileAddedFileId;
    SafePtr<std::map<std::string, VPNWorkers>> vpnUuidToVPNWorkers;
    std::optional<NetworkVPNType>              vpnConnectedType;
    std::string                                vpnLocalizationFileId;
};

#endif // VPN_TYPES_H
