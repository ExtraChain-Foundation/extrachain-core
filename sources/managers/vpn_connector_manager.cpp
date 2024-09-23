#include "managers/vpn_connector_manager.h"

#include <managers/extrachain_node.h>
#include <managers/account_controller.h>
#include <network/network_manager.h>
#include "datastorage/dfs/dfs_controller.h"

VPNConnectorManager::VPNConnectorManager(std::shared_ptr<ExtraChainNode> node, QObject *parent)
    : m_node(node), QObject { parent }
{
    vpnManager = raccoon::vpn::VPNManager::create();
    vpnInitPublicIPAndCountry = vpnManager->getPublicIPAndCountry();
    QCoreApplication *app = QCoreApplication::instance();
    m_workerThread = std::make_unique<VPNWorkerThread>(shared_from_this(), m_node, app);
    m_workerThread->start();
    QObject::connect(app, &QCoreApplication::aboutToQuit, m_workerThread.get(), [this]() {
        m_workerThread->stop();
        m_workerThread->wait();
    });
}

VPNConnectorManager::~VPNConnectorManager()
{
    qDebug() << "VPNConnectorManager::~VPNConnectorManager start destruction.";
    m_workerThread->stop();
    m_workerThread->wait();
    qDebug() << "VPNConnectorManager::~VPNConnectorManager destruction finished.";
}

bool VPNConnectorManager::CheckVPNHandshakeAccess(const std::string& requesterIdentifier, const int counter)
{
    qInfo() << "VPNConnectorManager::CheckVPNHandshakeAccess";
    qInfo() << "VPNConnectorManager::CheckVPNHandshakeAccess 1, size:" << vpnHandhakeCacheInProccess->size();
    bool requesterFound = true;
    auto vpnHandhakeCacheInProccessLocked = *vpnHandhakeCacheInProccess;
    for (auto it = vpnHandhakeCacheInProccessLocked->begin(); it != vpnHandhakeCacheInProccessLocked->end(); )
    {
        if (it->requesterIdentifier == requesterIdentifier)
        {
            qInfo() << "VPNConnectorManager::CheckVPNHandshakeAccess found";
            it->timestamp = QDateTime::currentDateTime();
            requesterFound = true;
            ++it;
        }
        else
        {
            QDateTime currentTime = QDateTime::currentDateTime();
            if (it->timestamp.secsTo(currentTime) >= 3)
            {
                qInfo() << "DELETED vpnHandhakeCacheInProccess" << it->uuid;
                it = vpnHandhakeCacheInProccessLocked->erase(it);
            }
            else
                ++it;
        }
    }

    if (requesterFound)
        return true;
    qInfo() << "VPNConnectorManager::CheckVPNHandshakeAccess not found" << (vpnHandhakeCacheInProccessLocked->size() < counter);
    return vpnHandhakeCacheInProccessLocked->size() < counter;
}

VPNWorkerThread::VPNWorkerThread(std::shared_ptr<VPNConnectorManager> vpnConnectorManager, std::shared_ptr<ExtraChainNode> node, QObject* parent)
    : QThread(parent), m_vpnConnectorManager(vpnConnectorManager), m_node(node), m_running(true) {}

void VPNWorkerThread::run()
{
    auto deleteFunc = [](const std::string& uuid, std::shared_ptr<ExtraChainNode> node, std::shared_ptr<VPNConnectorManager> vpnConnectorManager)
    {
        VPNFunctionsResult output;
        VPNMessage inputMsg;
        inputMsg.uuid = uuid;
        ActorId tempActorId;

        vpnConnectorManager->networkCallback(inputMsg, tempActorId, VPNFunctionType::DISCONNECT, output);
    };


    qDebug() << "VPNWorkerThread thread running...";
    while (m_running)
    {
        if (m_vpnConnectorManager->vpnConnectedType.has_value())
        {
            qInfo() << "MUTEX INSIDE VPNWorkerThread!";
            auto vpnUuidToVPNWorkersLocked = *m_vpnConnectorManager->vpnUuidToVPNWorkers;
            for (auto it = vpnUuidToVPNWorkersLocked->begin(); it != vpnUuidToVPNWorkersLocked->end(); ++it)
            {
                if (m_vpnConnectorManager->vpnConnectedType != VPNType::CLIENT)
                {
                    qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
                    if (currentTimestamp - it->second.lastUpdateRequsterTS >= 20000)
                    {
                        auto uuid = it->first;
                        deleteFunc(uuid, m_node, m_vpnConnectorManager);
                        break;
                    }
                }
                if (m_vpnConnectorManager->vpnConnectedType != VPNType::SERVER)
                {
                    bool isClient = m_vpnConnectorManager->vpnConnectedType == VPNType::CLIENT;
                    qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
                    if (currentTimestamp - it->second.lastUpdateNextTS >= 20000)
                    {
                        auto uuid = it->first;
                        deleteFunc(uuid, m_node, m_vpnConnectorManager);

                        if (isClient)
                            emit m_node->vpnDisconnect();
                        break;
                    }
                }

                qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
                if (m_vpnConnectorManager->vpnConnectedType != VPNType::SERVER && currentTimestamp - it->second.lastSendedNextTS >= 5000)
                {
                    it->second.lastSendedNextTS = QDateTime::currentMSecsSinceEpoch();

                    VPNMessage outputMsg;
                    outputMsg.uuid = it->second.uuid;
                    auto       mainActor = m_node->accountController()->mainActor();
                    MessageBody message   =
                        make_message(MessagePack::serialize(outputMsg), MessageType::VPNUpdateConnection, MessageStatus::Request, mainActor->id(), "");
                    auto        serialized = message.serialize();
                    auto        sign       = mainActor->key().sign(serialized);

                    auto newIdentifier = m_node->network()->foundCurrentIdentifier(it->second.nextIP, it->second.nextPort);
                    if (!newIdentifier.isEmpty())
                    {
                        qInfo() << "Send VPNUpdateConnection request!";
                        emit m_node->network()->sendNetworkMessage(serialized + sign, Config::Net::TypeSend::Focused, newIdentifier.toStdString());
                    }
                }
            }
        }

        QThread::sleep(2);
    }
    qDebug() << "VPNWorkerThread thread stopped.";
}

void VPNWorkerThread::stop()
{
    m_running = false;
}

bool VPNConnectorManager::networkCallback(VPNMessage& networkInput, ActorId& senderId, VPNFunctionType funcType, VPNFunctionsResult& output)
{
    static auto readFile = [](std::shared_ptr<ExtraChainNode> node, ActorId& senderId, const std::string& publicKeyFile) -> QString
    {
        // node.dfs()->requestFile(senderId, publicKeyFile);
        // return "";
        auto filePath = node->dfs()->getFileFromStorage(senderId, publicKeyFile);

        QFile file(QString::fromStdString(filePath));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        {
            qDebug() << "Could not open publicKey file storage:" << file.errorString();
            return "";
        }

        QTextStream in(&file);
        QString output = in.readAll();
        file.close();

        return output;
    };

    static auto convertIPv4ToIPv6 = [](const QString& ipv4) -> std::string
    {
        QHostAddress ipv4Addr(ipv4);
        if (ipv4Addr.protocol() != QAbstractSocket::IPv4Protocol) {
            qWarning() << "Invalid IPv4 address";
            return std::string();
        }

        quint32 ipv4Int = ipv4Addr.toIPv4Address();
        QString ipv6Mapped = QString("0:0:0:0:0:ffff:%1:%2")
                                 .arg((ipv4Int >> 16) & 0xffff, 4, 16, QLatin1Char('0'))
                                 .arg(ipv4Int & 0xffff, 4, 16, QLatin1Char('0'));

        return ipv6Mapped.toStdString();
    };

    switch (funcType)
    {
    case VPNFunctionType::GET_PUBLIC_IP:
    {
        auto ipCountry = vpnManager->getPublicIPAndCountry();
        if (!ipCountry.first.isEmpty())
        {
            output.str = ipCountry.first.toStdString();
            return true;
        }
        return false;
    }
    case VPNFunctionType::SET_CLIENT:
    {
        qInfo() << "SET_CLIENT: " << networkInput.publicIP << networkInput.publicKeyFile << senderId.toString();

        QString peerPublicKey = readFile(m_node, senderId, networkInput.publicKeyFile);
        if (peerPublicKey.isEmpty())
        {
            qDebug() << "PublicKey file is Empty";
            break;
        }
        qDebug() << "VPN PublicKey for client:" << peerPublicKey;
        std::string chainIndexStr = networkInput.resultChainIndex < 10 ? "0" + std::to_string(networkInput.resultChainIndex) : std::to_string(networkInput.resultChainIndex);


        raccoon::vpn::VPNManager::Config configuration{
            .interfaceName = "RACCOON_VPN_" + std::to_string(networkInput.resultChainIndex),
            .interfaceLocalIP = raccoon::IPLocalInterface("100.1" + chainIndexStr + ".0.1"),
            .interfaceDNS = "1.1.1.1",
            .postUp = {"ip rule add sport 22 table main",
                                  "ip rule add sport 2222 table main"},
            .postDown = {"ip rule del sport 22 table main",
                                  "ip rule del sport 2222 table main"},
            .peer =
            {
                {
                    .peerPublicKey = peerPublicKey.toStdString(),
                    .peerAllowedIPs = {"0.0.0.0/0", "::/0"},
                    .peerEndpoint = networkInput.publicIP + ":518" + chainIndexStr,
                    .peerPersistentKeepalive=21
                }
            }
        };

        vpnManager->connectAsClient(configuration);

        QThread::sleep(2);

        return vpnManager->isConnected();
    }
    case VPNFunctionType::CHECK_SERVER:
    {
        return vpnManager->canBeServer();
    }
    case VPNFunctionType::CHECK_PROXY:
    {
        return vpnManager->canBeProxy();
    }
    case VPNFunctionType::GET_LOCKED_CHAIN_INDEXES:
    {
        output.blockedChainIndexes = vpnManager->getBlockChainIndexes();
        return true;
    }
    case VPNFunctionType::SET_PROXY:
    {
        qInfo() << "SET_PROXY: " << networkInput.vpnType << networkInput.localIP << networkInput.publicKeyFile << senderId.toString();

        std::string localIp;
        QString nextPublicKey;
        QString requesterPublicKey;
        std::string nextPublicIP;
        std::list<std::string> requesterAllowedIPs;
        bool nextServer = false;
        auto vpnHandhakeCacheInProccessLocked = *vpnHandhakeCacheInProccess;
        auto savedIt = vpnHandhakeCacheInProccessLocked->begin();
        for (; savedIt != vpnHandhakeCacheInProccessLocked->end(); ++savedIt)
        {
            if (savedIt->chainIndex == networkInput.resultChainIndex && savedIt->uuid == networkInput.uuid)
            {
                localIp = savedIt->localIPForSetup;

                requesterPublicKey = readFile(m_node, savedIt->requesterId, savedIt->requesterPublicKeyFile);
                if (requesterPublicKey.isEmpty())
                {
                    qDebug() << "VPN PublicKey file requester is Empty";
                    break;
                }
                qDebug() << "VPN PublicKey requester for proxy:" << requesterPublicKey;

                nextPublicKey = readFile(m_node, senderId, savedIt->nextPublicKeyFile);
                if (nextPublicKey.isEmpty())
                {
                    qDebug() << "VPN PublicKey file next is Empty";
                    break;
                }
                qDebug() << "VPN PublicKey next for proxy:" << nextPublicKey;

                nextPublicIP = savedIt->nextPublicIP;


                for (const auto& it : savedIt->allIPsToSet)
                {
                    requesterAllowedIPs.emplace_back(it + "/32");
                    auto ipv6 = convertIPv4ToIPv6(QString::fromStdString(it));
                    if (!ipv6.empty())
                        requesterAllowedIPs.emplace_back(ipv6 + "/128");
                }

                if (savedIt->nextIdentifierType == VPNType::SERVER)
                    nextServer = true;
                break;
            }
        }

        if (localIp.empty() || nextPublicKey.isEmpty() || requesterPublicKey.isEmpty() || nextPublicIP.empty() || requesterAllowedIPs.empty())
        {
            qDebug("VPN PROXY setup error, some crucial value is EMPTY!");
            return false;
        }

        auto mainNetworkInterface = vpnManager->getMainNetworkInterface().toStdString();
        std::string portPostfix = networkInput.resultChainIndex < 10 ? "0" + std::to_string(networkInput.resultChainIndex) : std::to_string(networkInput.resultChainIndex);
        std::string interfaceName = "RACCOON_PROXY_" + std::to_string(networkInput.resultChainIndex);

        raccoon::vpn::VPNManager::Config configuration{
            .chainIndex = networkInput.resultChainIndex,
            .interfaceName = interfaceName,
            .interfaceLocalIP = raccoon::IPLocalInterface(localIp),
            .interfaceListenPort = "518" + portPostfix,
            .interfaceTable = "518" + portPostfix,
            .preUp = {"sysctl -w net.ipv4.ip_forward=1",
                                     "sysctl -w net.ipv6.conf.all.forwarding=1",
                                     "sysctl -w net.ipv6.conf.all.disable_ipv6=0",
                                     "sysctl -w net.ipv6.conf.default.disable_ipv6=0",
                                     "sysctl -w net.ipv6.conf.lo.disable_ipv6=0",
                                     "ip rule add iif " + interfaceName + " table 518" + portPostfix + " priority 456"},
            .postUp = {"ip rule add sport 22 table main",
                                     "ip rule add sport 2222 table main"},
            .postDown = {"ip rule del iif " + interfaceName + " table 518" + portPostfix + " priority 456",
                                     "ip rule del sport 22 table main",
                                     "ip rule del sport 2222 table main"},
            .peer = {
                {
                                   .peerPublicKey = requesterPublicKey.toStdString(),
                                   .peerAllowedIPs = requesterAllowedIPs,
                },
                {
                                   .peerPublicKey = nextPublicKey.toStdString(),
                                   .peerAllowedIPs = {"0.0.0.0/0", "::/0"},
                                   .peerEndpoint = nextPublicIP + (nextServer ? ":51900" : ":518" + portPostfix),
                                   .peerPersistentKeepalive=21
                }
            }
        };

        vpnManager->connectAsProxy(configuration);

        if (vpnManager->isConnected())
        {
            vpnHandhakeCacheInProccessLocked->erase(savedIt);
            return true;
        }
        else
            qDebug("VPN PROXY not connected");
        return false;
    }
    case VPNFunctionType::SET_SERVER:
    {
        qInfo() << "SET_SERVER: " << networkInput.vpnType << networkInput.localIP << networkInput.publicKeyFile << senderId.toString();

        QString peerPublicKey = readFile(m_node, senderId, networkInput.publicKeyFile);
        if (peerPublicKey.isEmpty())
        {
            qDebug() << "VPN PublicKey file is Empty";
            break;
        }
        qDebug() << "VPN PublicKey for server:" << peerPublicKey;

        if (networkInput.vpnType == VPNType::SERVER)
        {
            auto mainNetworkInterface = vpnManager->getMainNetworkInterface().toStdString();

            std::list<std::string> tempPeerAllowedIPs;
            for (const auto& it : networkInput.allIPsToSet)
            {
                tempPeerAllowedIPs.emplace_back(it + "/32");
                auto ipv6 = convertIPv4ToIPv6(QString::fromStdString(it));
                if (!ipv6.empty())
                    tempPeerAllowedIPs.emplace_back(ipv6 + "/128");
            }

            raccoon::vpn::VPNManager::Config configuration{
                .chainIndex = networkInput.resultChainIndex,
                .interfaceName = "RACCOON_SERVER",
                .interfaceLocalIP = raccoon::serverIP,
                .interfaceListenPort = "51900",
                .preUp = {"sysctl -w net.ipv4.ip_forward=1",
                                         "sysctl -w net.ipv6.conf.all.forwarding=1",
                                         "sysctl -w net.ipv6.conf.all.disable_ipv6=0",
                                         "sysctl -w net.ipv6.conf.default.disable_ipv6=0",
                                         "sysctl -w net.ipv6.conf.lo.disable_ipv6=0"},
                .postUp = {"ufw route allow in on RACCOON_SERVER out on " + mainNetworkInterface, "iptables -A FORWARD -i RACCOON_SERVER -o RACCOON_SERVER -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT; iptables -t nat -A POSTROUTING -o " + mainNetworkInterface + " -j MASQUERADE",
                                         "ip rule add sport 22 table main",
                                         "ip rule add sport 2222 table main"},
                .postDown = {"iptables -D FORWARD -i RACCOON_SERVER -o RACCOON_SERVER -m conntrack --ctstate RELATED,ESTABLISHED -j ACCEPT; iptables -t nat -D POSTROUTING -o " + mainNetworkInterface + " -j MASQUERADE", "ufw route delete allow in on RACCOON_SERVER out on " + mainNetworkInterface,
                                         "ip rule del sport 22 table main",
                                         "ip rule del sport 2222 table main"},
                .peer = {
                    {
                                       .peerPublicKey = peerPublicKey.toStdString(),
                                       .peerAllowedIPs = tempPeerAllowedIPs,
                    }
                }
            };

            vpnManager->connectAsServer(configuration);

            if (vpnManager->isConnected())
                return true;
            else
                qDebug("VPN SERVER not connected");
            return false;
        }
        else
            return false;
    }
    case VPNFunctionType::IS_CONNECTED:
    {
        return vpnManager->isConnected();
    }
    case VPNFunctionType::DISCONNECT:
    {
        auto vpnUuidToVPNWorkersLocked = *vpnUuidToVPNWorkers;
        auto res = vpnUuidToVPNWorkersLocked->find(networkInput.uuid);
        if (res != vpnUuidToVPNWorkersLocked->end())
        {
            vpnManager->disconnect(res->second.chainIndex);
            vpnUuidToVPNWorkersLocked->erase(res);
        }

        if (vpnUuidToVPNWorkers->empty())
            vpnConnectedType = {};

        return true;
    }
    default:
        break;
    }

    return false;
}

