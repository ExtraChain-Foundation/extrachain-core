/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

#include "datastorage/dfs/dfs_controller.h"
#include "managers/extrachain_node.h"
#include "managers/thread_pool.h"
#include "network/packages/base_message.h"
#include "network/upnpconnection.h"
#include "network/websocket_service.h"
#include "resolve/resolve_manager.h"

using namespace Messages;

const QList<SocketService *> &NetworkManager::connections() const {
    return m_connections;
}

bool NetworkManager::serverStatus(Network::Protocol protocol) const {
    switch (protocol) {
    case Network::Protocol::WebSocket:
        return wsServer == nullptr ? false : wsServer->isListening();
    case Network::Protocol::Undefined:
        return false;
    }
    return false;
}

void NetworkManager::setResolveManager(ResolveManager *value) {
    resolveManager = value;
}

NetworkManager::NetworkManager(ExtraChainNode *node) {
    this->node = node;
    connect(&m_networkStatus, &NetworkStatus::statusChanged,
            [](NetworkStatus::Status status) { qDebug() << "[NetworkStatus]" << status; });

    // if (m_networkStatus.status() == NetworkStatus::Status::Online) {
    // TODO: move to slot or process
    local = new QNetworkAddressEntry(Utils::findLocalIp(Utils::PrintDebug::Off));
    qDebug().noquote() << "[NetworkManager] Found local IP:" << local->ip().toString();
    // }

    if (local == nullptr) {
        qDebug() << "[NetworkManager] Local not found";
        return;
    }

    bool sub = local->ip().isInSubnet(QHostAddress::parseSubnet("192.168.0.0/16"));
    qDebug() << "Sub:" << sub;

    if (!sub) {
        // startDiscovery();
        return;
    }

    upnpDis = new UPNPConnection(*local);
    upnpNet = new UPNPConnection(*local);
    // connect(upnpNet, &UPNPConnection::success, this, &NetworkManager::);
    // connect(upnpDis, &UPNPConnection::success, this, &NetworkManager::startDiscovery);
    connect(upnpNet, &UPNPConnection::upnpError,
            [](QString msg) { qDebug() << "[NetworkManager] UPnP error:" << msg; });
    connect(upnpDis, &UPNPConnection::upnpError,
            [](QString msg) { qDebug() << "[NetworkManager] UPnP Discovery error:" << msg; });
    // qDebug() << "Tunnel creation started!";
    // upnpDis->makeTunnel(extPort, extPort, " UDP ", "Discovery tunnel of ExtraChain ");
    // upnpNet->makeTunnel(tcpPort, tcpPort, "TCP", "Network tunnel of ExtraChain ");
}

void NetworkManager::process() {
    auto tempTimer = new QTimer(this);
    connect(tempTimer, &QTimer::timeout, [this] { this->reconnection(); });
    tempTimer->start(5000);
}

void NetworkManager::reconnection() {
    for (const auto &el : qAsConst(m_reconnections)) {
        bool finded = false;
        for (SocketService *service : qAsConst(m_connections)) {
            // qDebug() << "Reconnection" << service << service->ip() << service->serverPort() << el.first;
            if (service->ip() == el.ip) {
                finded = true;
                break;
            }
        }

        if (finded)
            continue;

        qDebug().noquote() << "[NetworkManager]" << (tcpPort == 2222 ? "Node:" : "DFS:") << "Reconnection to"
                           << el.ip << el.protocol;
        connectToNode(el.ip, el.protocol);
    }
}

void NetworkManager::setupProxy(QNetworkProxy::ProxyType type, const QString &hostName, quint16 port,
                                const QString &user, const QString &password) {
    QNetworkProxy proxy;
    proxy.setType(type);
    proxy.setHostName(hostName);
    proxy.setPort(port);
    proxy.setUser(user);
    proxy.setPassword(password);
    QNetworkProxy::setApplicationProxy(proxy);
}

void NetworkManager::connectWsService(WebSocketService *service) {
    connect(service, &WebSocketService::error, this, &NetworkManager::socketError);
    connect(service, &WebSocketService::disconnected, this, &NetworkManager::removeWsConnection);
    if (!m_connections.contains(service))
        m_connections.append(service);
}

void NetworkManager::removeConnection(const QString &identifier) {
    if (identifier.isEmpty())
        qFatal("Try remove with empty identifier");

    for (auto connection : qAsConst(m_connections)) {
        if (connection->identifier() == identifier)
            emit connection->close();
    }
}

NetworkManager::~NetworkManager() {
    // delete resolverService;
    delete upnpNet;
    delete upnpDis;
    delete local;
    // delete discoveryService;

    for (const auto &connection : qAsConst(m_connections)) {
        emit connection->close();
        emit connection->finished();
    }
    m_connections.clear();
}

void NetworkManager::checkConnectionsStatus() {
    bool flag = false;
    int count = 0;
    std::for_each(m_connections.begin(), m_connections.end(), [&flag, &count](SocketService *el) {
        flag = flag || el->isActive();
        if (el->isActive())
            count++;
    });
    emit connectionStatusChanged(flag);
    emit connectionsCountChanged(count); // TODO: check prev count value

    if (flag) { // TODO: replace to networkStatusChanged slot
        sendFromCache();
        sendFromCacheOld();
    }
}

void NetworkManager::startNetwork() {
    qDebug() << "[NetworkManager] Start servers..." << (wsPort == 2233 ? "Network" : "DFS");

    if (local == nullptr) {
        qDebug() << "[NetworkManager] Can't detect local ip";
        return;
    }

    if (!Network::isStartedServer)
        return;
    wsServer = new QWebSocketServer("ExtraChain", QWebSocketServer::SslMode::NonSecureMode);

    if (wsServer->listen(QHostAddress::Any, wsPort)) {
        connect(wsServer, &QWebSocketServer::newConnection, this, &NetworkManager::onNewWsConnection);
        connect(wsServer, &QWebSocketServer::serverError, [](QWebSocketProtocol::CloseCode closeCode) {
            qDebug() << "[WS] Server error code:" << closeCode;
        });
        connect(wsServer, &QWebSocketServer::closed, [] { qDebug() << "[WS] Server: closed"; });
        connect(wsServer, &QWebSocketServer::acceptError, [](QAbstractSocket::SocketError socketError) {
            qDebug() << "[WS] Server socker error:" << socketError;
        });

        qDebug().noquote() << "[WS] Start listening" << wsServer->serverAddress().toString()
                           << wsServer->serverPort() << wsServer->serverName();
    }
}

void NetworkManager::startDiscovery() {
    qDebug() << "NetworkManager::startDiscovery()";
    // discoveryService = new DiscoveryService(extPort, tcpPort, local);
    // ThreadPool::addThread(discoveryService);
    // connect(discoveryService, &DiscoveryService::ClientDiscovered, this,
    // &NetworkManager::addConnectionFromPair);
}

void NetworkManager::connectToNode(const QString &ip, Network::Protocol protocol) {
    if (m_connections.length() >= Network::maxConnections) {
        qDebug() << "[NetworkManager] Can't connect because the maximum number of connections";
        return;
    }

    if (ip.isEmpty())
        return;

    const quint16 port = (protocol == wsPort);
    qDebug().noquote().nospace() << "[NetworkManager] Connect to " << ip << ", protocol: " << protocol
                                 << ", port: " << port;
    m_reconnections.insert(NetworkReconnect { .ip = ip, .port = port, .protocol = protocol });

    using Network::Protocol;
    switch (protocol) {
    case Protocol::WebSocket:
        connectToWebSocket(ip.simplified(), port);
        break;
    case Protocol::Undefined:
        qFatal("Undefined connectToNode");
    }
}

void NetworkManager::connectToWebSocket(const QString &ip, quint16 port) {
    auto service = new WebSocketService(nullptr, node);
    service->open(ip, port);
    connectWsService(service);
}

void NetworkManager::sendMessageOld(const QByteArray &message, const unsigned int &msgType,
                                    const SocketPair &receiver, Config::Net::TypeSend typeSend) {
    Config::Net::TypeSend send;

    if (typeSend == Config::Net::TypeSend::Default) {
        if (Messages::isChainMessage(msgType) || Messages::isGeneralRequest(msgType) || msgType == 400
            || msgType == 402)
            send = Config::Net::TypeSend::All;
        else if (Messages::isGeneralResponse(msgType) || msgType == 401 || msgType == 403)
            send = Config::Net::TypeSend::Focused;
        else
            send = Config::Net::TypeSend::Except;
    } else {
        send = typeSend;
    }

    if (!isActiveConnectionExists()) {
        // qDebug() << "[NetworkManager] Saved message to cache";
        saveToCacheOld(message, msgType, receiver, send);
    }

    auto isSendCheck = [](Config::Net::TypeSend send, std::string_view socketIp, quint16 socketPort,
                          const SocketPair &pair) {
        switch (send) {
        case Config::Net::TypeSend::Except:
            return socketIp != pair.ip && socketPort != pair.port;
            break;
        case Config::Net::TypeSend::Focused:
            return socketIp == pair.ip && socketPort == pair.port;
            break;
        case Config::Net::TypeSend::All:
            return true;
            break;
        default:
            return false;
            break;
        }
    };

    for (const auto &service : qAsConst(m_connections)) {
        bool isSend = isSendCheck(send, service->ip().toStdString(), service->port(), receiver);
        if (!isSend)
            continue;
        if (service->isActive() && service->sendType() == SocketService::SendType::All)
            emit service->send(message);
    }
}

void NetworkManager::sendMessage(const std::string &serialized_message, Config::Net::TypeSend typeSend,
                                 const std::string &receiver_identifier) {
    if (!isActiveConnectionExists()) {
        qDebug() << "[NetworkManager] Save message to cache";
        saveToCache(serialized_message, typeSend, receiver_identifier);
    }

    auto isSendCheck = [typeSend, receiver_identifier](std::string_view socket_identifier) {
        switch (typeSend) {
        case Config::Net::TypeSend::Except:
            return socket_identifier != receiver_identifier;
            break;
        case Config::Net::TypeSend::Focused:
            return socket_identifier == receiver_identifier;
            break;
        case Config::Net::TypeSend::All:
            return true;
            break;
        default:
            return false;
            break;
        }
    };

    for (const auto &service : qAsConst(m_connections)) {
        bool isSend = isSendCheck(service->identifier().toStdString());
        if (!isSend)
            continue;
        if (service->isActive() && service->sendType() == SocketService::SendType::All)
            emit service->send(QByteArray::fromStdString(serialized_message));
    }
}

void NetworkManager::saveToCache(const std::string &serialized_message, Config::Net::TypeSend typeSend,
                                 const std::string &receiver_identifier) {
    std::ofstream file;
    file.open("tmp/network.cache", std::ios_base::out | std::ios_base::app);
    if (!file.is_open()) {
        qFatal("[NetworkManager/saveToCache] Error open cache file");
    }

    std::tuple<std::string, Config::Net::TypeSend, std::string> tuple = { serialized_message, typeSend,
                                                                          receiver_identifier };
    std::string package = MessagePack::serialize(tuple);
    file << Utils::intToStdString(int(package.length()), 8);
    file << package;

    file.close();
}

void NetworkManager::sendFromCache() {
    QFile file("tmp/network.cache");
    if (!file.exists() || !file.open(QFile::ReadOnly)) {
        return;
    }

    QByteArrayList allPackages = Serialization::deserialize(file.readAll(), 8);
    file.close();
    file.remove();

    for (const QByteArray &packageData : qAsConst(allPackages)) {
        auto [serialized_message, typeSend, receiver_identifier] =
            MessagePack::deserializeQt<std::tuple<std::string, Config::Net::TypeSend, std::string>>(
                packageData);
        sendMessage(serialized_message, typeSend, receiver_identifier);
    }
}

bool NetworkManager::isActiveConnectionExists() {
    if (this->m_connections.isEmpty())
        return false;

    for (const auto &el : qAsConst(this->m_connections)) {
        if (el->isActive())
            return true;
    }

    return false;
}

bool NetworkManager::checkMsgCount(const QByteArray &msg) {
    bool flag_result = true;
    bool value = 0;
    QByteArray hashMsg = Utils::calcKeccak(msg);
    QMap<QByteArray, int>::iterator it = msgHashList.find(hashMsg);

    if (it == msgHashList.end())
        msgHashList.insert(hashMsg, value);
    else {
        if (msgHashList.find(hashMsg).value() == m_connections.length() - 1) {
            msgHashList.remove(hashMsg);
            flag_result = false;
        } else {
            msgHashList.find(hashMsg).value()++;
            flag_result = true;
        }
    }

    return flag_result;
}

void NetworkManager::saveToCacheOld(const QByteArray &message, const unsigned int &msgType,
                                    const SocketPair &receiver, Config::Net::TypeSend typeSend) {
    QFile file("tmp/network_old.cache");
    file.open(QFile::Append);
    QByteArrayList list = { message,
                            QByteArray::fromStdString(receiver.ip),
                            QByteArray::number(receiver.port),
                            receiver.m_identifier,
                            QByteArray::number(msgType),
                            QByteArray::number(int(typeSend)) };
    QByteArray package = Serialization::serialize(list, 8);
    file.write(Utils::intToByteArray(package.length(), 8) + package);
    file.close();
}

void NetworkManager::sendFromCacheOld() {
    QFile file("tmp/network_old.cache");
    if (!file.exists() || !file.open(QFile::ReadOnly)) {
        return;
    }

    QByteArrayList allPackages = Serialization::deserialize(file.readAll(), 8);
    file.close();
    file.remove();

    for (const QByteArray &packageData : qAsConst(allPackages)) {
        QByteArrayList package = Serialization::deserialize(packageData, 8);
        if (package.length() != 6)
            return;

        QByteArray data = package[0];
        SocketPair socketData;
        socketData.ip = package[1].toStdString();
        socketData.port = package[2].toShort();
        socketData.m_identifier = package[3];
        // TODO: protocol
        auto msgType = package[4].toUInt();
        Config::Net::TypeSend typeSend = Config::Net::TypeSend(package[5].toInt());
        sendMessageOld(data, msgType, socketData, typeSend);
    }
}

void NetworkManager::messageReceivedOld(const QByteArray &msg, const SocketPair &receiver) {
    if (checkMsgCount(msg)) {
        if (msg.left(6) == "ExCNew") {
            messageReceived(msg.mid(6).toStdString(), receiver.m_identifier.toStdString());
            return;
        }

        resolveManager->setTask(msg, receiver);
    } else {
        qDebug()
            << "[Network Manager] checkMsgCount have returned false: such message has been already added";
    }
}

void NetworkManager::messageReceived(const std::string &message, const std::string &receiver) {
    qDebug() << "[NetworkManager/messageReceived] New message type";
    std::string_view msg(message.begin(), message.end() - 64);
    std::string_view sign(message.end() - 64, message.end());
    // std::cout << "[NetworkManager/messageReceived] " << sign << " " << msg << std::endl;

    {
        auto sender = std::string(msg.begin() + 20, msg.begin() + 40);
        auto actor = node->actorIndex()->getActor(sender);

        bool verify = actor.key().verify(QByteArray::fromStdString(std::string(msg)),
                                         QByteArray::fromStdString(std::string(sign)));
        if (!verify) {
            // qDebug() << "[NetworkManager/messageReceived] Error verify message";
        } else {
            qDebug() << "[NetworkManager/messageReceived] Verify good";
        }
    }

    auto type =
        MessagePack::deserialize<MessageType>(std::string_view(msg.begin() + 1, msg.begin() + 2)).first;
    auto status =
        MessagePack::deserialize<MessageStatus>(std::string_view(msg.begin() + 2, msg.begin() + 3)).first;
    auto serialized = std::string_view(msg.begin() + 40, msg.end());
    auto messId = std::string(msg.begin() + 4, msg.begin() + 19);

    if (type == MessageType::Actor && status == MessageStatus::Request) { }

    try {
        switch (type) {
        case MessageType::Actor: {
            // actor get, test use ActorId
            if (status == MessageStatus::Request) {
                auto [actorId, success] = MessagePack::deserialize<std::string>(serialized);
                node->actorIndex()->handleGetActor(actorId, receiver);
            } else if (status == MessageStatus::Response) {
                auto [actor, success] = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
                node->actorIndex()->handleNewActor(actor);
            }
            break;
        }
        case MessageType::ActorAll: {
            // node->actorIndex()->handleGetAllActor(messId);
            break;
        }
        case MessageType::ActorCount: {
            break;
        }

        case MessageType::DfsAddFile: {
            auto [msg, success] = MessagePack::deserialize<DFS::Packets::AddFileMessage>(serialized);
            node->dfs()->addFile(msg, true);
            break;
        }
        case MessageType::DfsRequestFileSegment: {
            auto [msg, success] =
                MessagePack::deserialize<DFS::Packets::RequestFileSegmentMessage>(serialized);
            node->dfs()->sendFragment(msg);
            break;
        }
        case MessageType::DfsAddSegment: {
            auto [msg, success] = MessagePack::deserialize<DFS::Packets::AddSegmentMessage>(serialized);
            node->dfs()->addFragment(msg);
            break;
        }
        case MessageType::DfsEditSegment: {
            auto [msg, success] = MessagePack::deserialize<DFS::Packets::EditSegmentMessage>(serialized);
            node->dfs()->insertFragment(msg);
            break;
        }
        case MessageType::DfsDeleteSegment: {
            auto [msg, success] = MessagePack::deserialize<DFS::Packets::DeleteSegmentMessage>(serialized);
            node->dfs()->deleteFragment(msg);
            break;
        }
        case MessageType::DfsRemoveFile: {
            auto [msg, success] = MessagePack::deserialize<DFS::Packets::RemoveFileMessage>(serialized);
            node->dfs()->removeFile(msg);
            break;
        }

        default:
            qFatal("[NetworkManager/messageReceived] Not supported message type: %d", int(type));
            break;
        }
    } catch (std::exception e) { qFatal("[NetworkManager/messageReceived] Error deserialize"); }
}

void NetworkManager::removeWsConnection() //
{
    if (QObject::sender() == nullptr)
        return;

    auto connection = qobject_cast<SocketService *>(QObject::sender());
    auto removed = m_connections.removeAll(connection);
    if (removed == 0)
        return;
    qDebug() << "[WS] Removed" << connection;
    connection->deleteLater();
    checkConnectionsStatus();
}

void NetworkManager::socketError(Network::SocketServiceError error, QString errorData) {
    if (QObject::sender() == nullptr) {
        return;
    }

    auto service = qobject_cast<SocketService *>(QObject::sender());
    qDebug() << "[NetworkManager] Error socket:" << error << service->identifier();

    if (error != Network::SocketServiceError::DuplicateIdentifier) {
        auto res = std::find_if(m_reconnections.begin(), m_reconnections.end(),
                                [service](const NetworkReconnect &recon) {
                                    return recon.ip == service->ip() && recon.protocol == service->protocol();
                                });
        if (res != m_reconnections.end()) {
            m_reconnections.remove(*res);
        }
    }

    if (error == Network::IncompatibleNetwork || error == Network::IncompatibleVersion) {
        emit connectionError(error, service->identifier(), errorData);
    }
}

QString NetworkManager::localIp() {
    return local->ip().toString();
}

void NetworkManager::send(const QByteArray &data, const unsigned int &msgType, const SocketPair &receiver,
                          Config::Net::TypeSend typeSend) {
    Messages::BaseMessage msg;
    msg.type = msgType;
    msg.data = data;
    QByteArray message = msg.serialize();
    sendMessageOld(message, msgType, receiver, typeSend);
}

void NetworkManager::onNewWsConnection() {
    auto ws = wsServer->nextPendingConnection();
    if (ws == nullptr)
        qFatal("[WS] Error: ws == nulltpr");

    if (m_connections.length() >= Network::maxConnections) {
        qDebug() << "[NetworkManager] Can't connect from WS server because the maximum number of connections";
        return;
    }

    auto service = new WebSocketService(ws, node);
    connectWsService(service);
    emit newSocket();
}
