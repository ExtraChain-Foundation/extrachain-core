#ifndef VPN_CONNECTOR_MANAGER_H
#define VPN_CONNECTOR_MANAGER_H

#include <atomic>

#include <QObject>
#include <QThread>

#include "network/message_body.h"
#include "vpnmanager.h"
#include "utils/safeptr.h"

class VPNMessage;
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

class VPNConnectorManager : public QObject, std::enable_shared_from_this<VPNConnectorManager> {
    Q_OBJECT
public:
    explicit VPNConnectorManager(std::shared_ptr<ExtraChainNode> node, QObject *parent = nullptr);

    ~VPNConnectorManager();

    bool CheckVPNHandshakeAccess(const std::string& requesterIdentifier, const int counter);

    bool networkCallback(VPNMessage& networkInput, ActorId& senderId, VPNFunctionType funcType, VPNFunctionsResult& output);

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
        qint64  lastUpdateRequsterTS;
        qint64  lastUpdateNextTS;
        qint64  lastSendedNextTS;
    };

    SafePtr<QList<VPNHandhakeCache>> vpnHandhakeCacheInProccess;

    std::atomic_bool vpnIsClient = false;
    std::vector<std::string>         vpnFileAddedHash;
    QString             vpnFileLocalPath;
    std::pair<QString, QString> vpnInitPublicIPAndCountry;

    SafePtr<std::map<std::string, VPNWorkers>> vpnUuidToVPNWorkers;
    std::optional<VPNType> vpnConnectedType;
    std::optional<std::tuple<QString, quint16, QString>> vpnLastDestroyed;

    std::shared_ptr<raccoon::vpn::VPNManager> vpnManager;
private:
    std::unique_ptr<VPNWorkerThread> m_workerThread;
    std::shared_ptr<ExtraChainNode> m_node;
};

class VPNWorkerThread : public QThread
{
    Q_OBJECT

public:
    VPNWorkerThread(std::shared_ptr<VPNConnectorManager> vpnConnectorManager, std::shared_ptr<ExtraChainNode> node, QObject* parent = nullptr);

    void run() override;

    void stop();

private:
    std::shared_ptr<VPNConnectorManager> m_vpnConnectorManager;
    std::shared_ptr<ExtraChainNode> m_node;
    std::atomic_bool m_running;
};

#endif // VPN_CONNECTOR_MANAGER_H
