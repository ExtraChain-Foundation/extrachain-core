#include "managers/vpn_connector_manager.h"

#include <managers/extrachain_node.h>
#include <managers/account_controller.h>
#include <network/network_manager.h>

VPNConnectorManager::VPNConnectorManager(ExtraChainNode* node, std::function<bool(ExtraChainNode&, VPNMessage&, ActorId&, VPNFunctionType, VPNFunctionsResult&)> vpnFunctions, QObject *parent)
    : vpnFunctions(vpnFunctions), m_node(node), QObject { parent }
{
    QCoreApplication *app = QCoreApplication::instance();
    m_workerThread = std::make_unique<VPNWorkerThread>(this, m_node, app);
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
    qInfo() << "MUTEX 4";
    std::lock_guard<std::mutex> lock(vpnHandhakeCacheMutex);
    qInfo() << "VPNConnectorManager::CheckVPNHandshakeAccess 1, size:" << vpnHandhakeCacheInProccess.size();
    bool requesterFound = true;
    for (auto it = vpnHandhakeCacheInProccess.begin(); it != vpnHandhakeCacheInProccess.end(); )
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
                it = vpnHandhakeCacheInProccess.erase(it);
            }
            else
                ++it;
        }
    }

    if (requesterFound)
        return true;
    qInfo() << "VPNConnectorManager::CheckVPNHandshakeAccess not found" << (vpnHandhakeCacheInProccess.size() < counter);
    return vpnHandhakeCacheInProccess.size() < counter;
}

VPNWorkerThread::VPNWorkerThread(VPNConnectorManager* vpnManager, ExtraChainNode* node, QObject* parent)
    : QThread(parent), m_vpnManager(vpnManager), m_node(node), m_running(true) {}

void VPNWorkerThread::run()
{
    auto deleteFunc = [](const std::string& uuid, ExtraChainNode* node, VPNConnectorManager* vpnManager)
    {
        VPNFunctionsResult output;
        VPNMessage inputMsg;
        inputMsg.uuid = uuid;
        ActorId tempActorId;

        vpnManager->vpnFunctions(*node, inputMsg, tempActorId, VPNFunctionType::DISCONNECT, output);
    };


    qDebug() << "VPNWorkerThread thread running...";
    while (m_running)
    {
        if (m_vpnManager->vpnConnectedType.has_value())
        {
            qInfo() << "MUTEX INSIDE VPNWorkerThread!";
            std::unique_lock<std::mutex> lock(m_vpnManager->vpnUuidToVPNWorkersMutex);
            for (auto it = m_vpnManager->vpnUuidToVPNWorkers.begin(); it != m_vpnManager->vpnUuidToVPNWorkers.end(); ++it)
            {
                if (m_vpnManager->vpnConnectedType != VPNType::CLIENT)
                {
                    qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
                    if (currentTimestamp - it->second.lastUpdateRequsterTS >= 20000)
                    {
                        auto uuid = it->first;
                        lock.unlock();
                        deleteFunc(uuid, m_node, m_vpnManager);
                        break;
                    }
                }
                if (m_vpnManager->vpnConnectedType != VPNType::SERVER)
                {
                    bool isClient = m_vpnManager->vpnConnectedType == VPNType::CLIENT;
                    qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
                    if (currentTimestamp - it->second.lastUpdateNextTS >= 20000)
                    {
                        auto uuid = it->first;
                        lock.unlock();
                        deleteFunc(uuid, m_node, m_vpnManager);

                        if (isClient)
                            emit m_node->vpnDisconnect();
                        break;
                    }
                }

                qint64 currentTimestamp = QDateTime::currentMSecsSinceEpoch();
                if (m_vpnManager->vpnConnectedType != VPNType::SERVER && currentTimestamp - it->second.lastSendedNextTS >= 5000)
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
