#ifndef VPN_TYPES_H
#define VPN_TYPES_H

#include "utils/safeptr.h"

class ActorId;
enum class VPNType;

struct VPNConfigStorage {
    struct VPNHandhakeCache {
        std::string              uuid;
        std::string              requesterIdentifier;
        std::string              nextIdentifier;
        VPNType                  nextIdentifierType;
        int                      chainIndex;
        int                      proxyIndex;
        int                      internalIndex;
        QDateTime                timestamp = QDateTime();
        std::string              proxyResponseMessageID;
        std::string              localIPForSetup;
        std::string              proxyCounter;
        std::vector<std::string> allIPsToSet;
        std::string              requesterPublicKeyFile;
        ActorId                  requesterId;
        std::string              nextPublicKeyFile;
        std::string              nextPublicIP;
    };

    struct VPNWorkers {
        std::string uuid;
        int         chainIndex;
        std::string requesterIdentifier;
        QString     requesterIP;
        quint16     requesterPort;
        std::string nextIdentifier;
        QString     nextIP;
        quint16     nextPort;
        qint64      lastUpdateRequsterTS;
        qint64      lastUpdateNextTS;
        qint64      lastSendedNextTS;
    };

    SafePtr<QList<VPNHandhakeCache>> vpnHandhakeCacheInProccess;

    std::atomic_bool                           vpnIsClient = false;
    std::vector<std::string>                   vpnFileAddedHash;
    std::pair<QString, QString>                vpnInitPublicIPAndCountry;
    SafePtr<std::map<std::string, VPNWorkers>> vpnUuidToVPNWorkers;
    std::optional<VPNType>                     vpnConnectedType;
    std::string                                vpnLocalizationFileHash;
};

#endif // VPN_TYPES_H
