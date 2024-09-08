#ifndef VPN_CONNECTOR_MANAGER_H
#define VPN_CONNECTOR_MANAGER_H

#include <QObject>
#include <QThread>

#include "network/message_body.h"

class ActorId;
class ExtraChainNode;

enum class VPNFunctionType
{
    SET_CLIENT,
    SET_SERVER,
    SET_PROXY,
    CHECK_SERVER,
    CHECK_PROXY,
    GET_PUBLIC_IP,
    IS_CONNECTED,
    DISCONNECT,
    GET_LOCKED_CHAIN_INDEXES
};

struct VPNFunctionsResult
{
    std::string str;
    std::set<int> blockedChainIndexes;
};

class VPNWorkerThread;

class VPNConnectorManager : public QObject {
    Q_OBJECT
public:
    explicit VPNConnectorManager(ExtraChainNode* node, std::function<bool(ExtraChainNode&, VPNMessage&, ActorId&, VPNFunctionType, VPNFunctionsResult&)> vpnFunctions = nullptr, QObject *parent = nullptr);

    ~VPNConnectorManager();

    bool CheckVPNHandshakeAccess(const std::string& requesterIdentifier, const int counter);

    struct VPNHandhakeCache
    {
        std::string uuid;
        std::string requesterIdentifier;
        std::string nextIdentifier;
        VPNType nextIdentifierType;
        int chainIndex;
        int proxyIndex;
        int internalIndex;
        QDateTime timestamp = QDateTime();
        std::string proxyResponseMessageID;
        std::string localIPForSetup;
        std::string proxyCounter;
        std::vector<std::string> allIPsToSet;
        std::string requesterPublicKeyFile;
        ActorId requesterId;
        std::string nextPublicKeyFile;
        std::string nextPublicIP;
    };

    struct VPNWorkers
    {
        std::string uuid;
        int chainIndex;
        std::string requesterIdentifier;
        QString requesterIP;
        quint16 requesterPort;
        std::string nextIdentifier;
        QString nextIP;
        quint16 nextPort;
        QDateTime lastUpdateRequsterTS;
        QDateTime lastUpdateNextTS;
        QDateTime lastSendedNextTS;
    };


    std::mutex vpnHandhakeCacheMutex;
    QList<VPNHandhakeCache> vpnHandhakeCacheInProccess;

    std::mutex vpnLockedChainIndexesMutex;
    std::set<int> vpnLockedChainIndexes;

    bool vpnIsClient = false;
    std::vector<std::string>         vpnFileAddedHash;
    QString             vpnFileLocalPath;
    std::function<bool(ExtraChainNode&, VPNMessage&, ActorId&, VPNFunctionType, VPNFunctionsResult&)> vpnFunctions;
    std::pair<QString, QString> vpnInitPublicIPAndCountry;

    std::mutex vpnUuidToVPNWorkersMutex;
    std::map<std::string, VPNWorkers> vpnUuidToVPNWorkers;
    std::optional<VPNType> vpnConnectedType;
    std::optional<std::tuple<QString, quint16, QString>> vpnLastDestroyed;

private:
    std::unique_ptr<VPNWorkerThread> m_workerThread;
    ExtraChainNode* m_node;
};

class VPNWorkerThread : public QThread
{
    Q_OBJECT

public:
    VPNWorkerThread(VPNConnectorManager* vpnManager, ExtraChainNode* node, QObject* parent = nullptr);

    void run() override;

    void stop();

private:
    VPNConnectorManager* m_vpnManager;
    ExtraChainNode* m_node;
    std::atomic_bool m_running;
};

#endif // VPN_CONNECTOR_MANAGER_H
