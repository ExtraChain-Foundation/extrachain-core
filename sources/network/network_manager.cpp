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
#include "network/upnpconnection.h"
#include "network/websocket_service.h"
#include <fstream>

const QList<SocketService *> &NetworkManager::connections() const {
    return m_connections;
}

bool NetworkManager::serverStatus(Network::Protocol protocol) const {
    switch (protocol) {
    case Network::Protocol::Udp:
        break;
    case Network::Protocol::WebSocket:
        return wsServer == nullptr ? false : wsServer->isListening();
    case Network::Protocol::Undefined:
        return false;
    }
    return false;
}

NetworkManager::NetworkManager(ExtraChainNode &node)
    : node(node) {
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

        qDebug().noquote() << "[NetworkManager] Reconnection to" << el.ip << el.protocol;
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
    connect(service, &WebSocketService::activated, this, &NetworkManager::checkConnectionsStatus);

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

    const quint16 port = (protocol == Network::Protocol::WebSocket ? wsPort : 0);
    qDebug().noquote().nospace() << "[NetworkManager] Connect to " << ip << ", protocol: " << protocol
                                 << ", port: " << port;
    m_reconnections.insert(NetworkReconnect { .ip = ip, .port = port, .protocol = protocol });

    using Network::Protocol;
    switch (protocol) {
    case Protocol::Udp:
        break;
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

void NetworkManager::sendMessage(const std::string &serialized_message, Config::Net::TypeSend typeSend,
                                 const std::string &receiver_identifier) {
    if (!isActiveConnectionExists()) {
        qDebug() << "[NetworkManager] Save message to cache";
        saveToCache(serialized_message, typeSend, receiver_identifier);
        return;
    }

    auto isSendCheck = [typeSend, receiver_identifier](std::string_view socket_identifier) {
        switch (typeSend) {
        case Config::Net::TypeSend::Except:
            return socket_identifier != receiver_identifier;
        case Config::Net::TypeSend::Focused:
            return socket_identifier == receiver_identifier;
        case Config::Net::TypeSend::All:
            return true;
        default:
            return false;
        }
    };

    for (const auto &service : qAsConst(m_connections)) {
        if (service->isActive() && service->sendType() == SocketService::SendType::All) {
            service->sendMessage(QByteArray::fromStdString(serialized_message));
        }
    }
}

void NetworkManager::saveToCache(const std::string &serialized_message, Config::Net::TypeSend typeSend,
                                 const std::string &receiver_identifier) {
    std::ofstream file;
    file.open("tmp/network.cache", std::ios_base::out | std::ios_base::app | std::ios_base::binary);
    if (!file.is_open()) {
        qFatal("[NetworkManager/saveToCache] Error open cache file");
    }

    std::tuple tuple = { serialized_message, typeSend, receiver_identifier };
    std::string package = MessagePack::serialize(tuple);
    file << Utils::intToStdString(int(package.length()), 8);
    file << package;

    file.close();
}

void NetworkManager::sendFromCache() {
    qDebug() << "[NetworkManager] Load from cache";

    QFile file("tmp/network.cache");
    if (!file.exists() || !file.open(QFile::ReadOnly)) {
        return;
    }

    QByteArrayList allPackages = Serialization::deserialize(file.readAll(), 8);
    file.close();
    file.remove();

    for (const QByteArray &packageData : qAsConst(allPackages)) {
        auto [serialized_message, typeSend, receiver_identifier] =
            MessagePack::deserialize<std::tuple<std::string, Config::Net::TypeSend, std::string>>(
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
    QByteArray hashMsg = Utils::calcHash(msg);
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

void NetworkManager::messageReceived(const std::string &message, const std::string &identifier) {
    //    if (m_messages.contains(identifier)) {
    //        qDebug() << "[[Network Manager] current message contains in messages by identifier ";
    //        return;
    //    }

    if (!checkMsgCount(QByteArray::fromStdString(message))) { // TODO: remove byte array
        qDebug()
            << "[Network Manager] checkMsgCount have returned false: such message has been already added";
        return;
    }

    m_messages[identifier] = message;

    std::string_view msg = std::string_view(message).substr(0, message.size() - 64);
    std::string_view sign = std::string_view(message).substr(message.size() - 64, 64);

    // TODO: no check new actor
    //    {
    //        auto sender = std::string(msg.begin() + 20, msg.begin() + 40);
    //        auto actor = node.actorIndex()->getActor(sender);

    //        bool verify = actor.key().verify(QByteArray::fromStdString(std::string(msg)),
    //                                         QByteArray::fromStdString(std::string(sign)));
    //        if (!verify) {
    //            // qDebug() << "[NetworkManager/messageReceived] Error verify message";
    //        } else {
    //            qDebug() << "[NetworkManager/messageReceived] Verify good";
    //        }
    //    }

    MessageType type = MessagePack::deserialize<MessageType>(msg.substr(1, 1));
    auto status = MessagePack::deserialize<MessageStatus>(msg.substr(2, 1));
    auto serialized = msg.substr(40);
    auto messId = msg.substr(4, 15);
    std::string messageId(messId.begin(), messId.end());

    if (status == MessageStatus::Request) {
        m_messages[messageId] = identifier;
    }

#ifdef QT_DEBUG
    if (Network::networkDebug) {
        msgpack::object_handle oh = msgpack::unpack(serialized.data(), serialized.size());
        msgpack::object deserialized = oh.get();
        qDebug() << fmt::format("[Network Message] Received: type {}, status {}, id {}, body: {}", type,
                                status, messId, (std::stringstream() << deserialized).str())
                        .c_str();
    }
#endif

    // try {
    switch (type) {
    case MessageType::NewActor: {
        auto actor = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
        node.actorIndex()->handleNewActor(actor);
        break;
    }
    case MessageType::Actor: {
        // actor get, test use ActorId
        if (status == MessageStatus::Request) {
            auto actorId = MessagePack::deserialize<ActorId>(serialized);
            node.actorIndex()->handleGetActor(actorId, messageId);
        } else if (status == MessageStatus::Response) {
            auto actor = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
            node.actorIndex()->handleNewActor(actor);
        }
        break;
    }
    case MessageType::ActorAll: {
        if (status == MessageStatus::Request) {
            auto ignoredActorId = MessagePack::deserialize<ActorId>(serialized);
            node.actorIndex()->handleGetAllActor(ignoredActorId, messageId);
        } else if (status == MessageStatus::Response) {
            auto actors = MessagePack::deserialize<std::vector<std::string>>(serialized);
            node.actorIndex()->handleNewAllActors(actors);
        }
        break;
    }
    case MessageType::ActorCount:
        break;

    case MessageType::DfsDirData: {
        if (status == MessageStatus::Request) {
            auto actorId = MessagePack::deserialize<ActorId>(serialized); // TODO: add last modified
            node.dfs()->sendDirData(actorId, 0, messageId);
        } else if (status == MessageStatus::Response) {
            auto [actorId, dirRows] =
                MessagePack::deserialize<std::pair<ActorId, std::vector<DFSP::DirRow>>>(serialized);
            node.dfs()->addDirData(actorId, dirRows);
        }
        break;
    }
    case MessageType::DfsLastModified: {
        auto msg = MessagePack::deserialize<uint64_t>(serialized);
        node.dfs()->sendSync(msg, messageId);
        break;
    }
    case MessageType::DfsAddFile: {
        auto msg = MessagePack::deserialize<DFSP::AddFileMessage>(serialized);
        node.dfs()->addFile(msg, true);
        break;
    }
    case MessageType::DfsRequestFile: {
        auto [actorId, fileName] = MessagePack::deserialize<std::pair<ActorId, std::string>>(serialized);
        node.dfs()->sendFile(actorId, fileName, messageId);
        break;
    }
    case MessageType::DfsRequestFileSegment: {
        auto msg = MessagePack::deserialize<DFSP::RequestFileSegmentMessage>(serialized);
        emit fetchFragments(msg, messageId);
        break;
    }
    case MessageType::DfsAddSegment: {
        auto msg = MessagePack::deserialize<DFSP::SegmentMessage>(serialized);
        emit addFragSignal(msg);
        break;
    }
    case MessageType::DfsEditSegment: {
        auto msg = MessagePack::deserialize<DFSP::SegmentMessage>(serialized);
        node.dfs()->insertFragment(msg);
        break;
    }
    case MessageType::DfsDeleteSegment: {
        auto msg = MessagePack::deserialize<DFSP::DeleteSegmentMessage>(serialized);
        node.dfs()->deleteFragment(msg);
        break;
    }
    case MessageType::DfsRemoveFile: {
        auto msg = MessagePack::deserialize<DFSP::RemoveFileMessage>(serialized);
        node.dfs()->removeFile(msg);
        break;
    }
    case MessageType::DfsSendingFileDone: { // TODO
        auto [actorId, fileHash] = MessagePack::deserialize<std::pair<ActorId, std::string>>(serialized);
        qDebug() << "[Dfs] File done:" << actorId << fileHash.c_str();
        break;
    }

    default:
        qFatal("[NetworkManager/messageReceived] Not supported message type: %d", int(type));
        break;
    }
    // } catch (std::exception e) { qFatal("[NetworkManager/messageReceived] Error deserialize"); }
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

    if (error == Network::SocketServiceError::IncompatibleNetwork
        || error == Network::SocketServiceError::IncompatibleVersion) {
        emit connectionError(error, service->identifier(), errorData);
    }
}

QString NetworkManager::localIp() {
    return local->ip().toString();
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
