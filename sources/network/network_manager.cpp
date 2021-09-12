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

#include "network/network_manager.h"
#include "resolve/resolve_manager.h"

using namespace Messages;

const QList<SocketService *> &NetworkManager::connections() const
{
    return m_connections;
}

bool NetworkManager::serverStatus(Network::Protocol protocol) const
{
    switch (protocol)
    {
    case Network::Protocol::Tcp:
        return tcpServer == nullptr ? false : tcpServer->isListening();
    case Network::Protocol::WebSocket:
        return wsServer == nullptr ? false : wsServer->isListening();
    case Network::Protocol::Undefined:
        return false;
    }
}

void NetworkManager::setResolveManager(ResolveManager *value)
{
    resolveManager = value;
}

ActorIndex *NetworkManager::actorIndex() const
{
    return m_actorIndex;
}

NetworkManager::NetworkManager(ActorIndex *actorIndex, const QString &localIp)
{
    this->m_actorIndex = actorIndex;

    if (localIp.isEmpty())
    {
        local = new QNetworkAddressEntry(Utils::findLocalIp(Utils::PrintDebug::Off));
        qDebug().noquote() << "[NetworkManager] Found local IP:" << local->ip().toString();
    }
    else
    {
        qDebug().noquote() << "[NetworkManager] Set local IP from settings:" << localIp;
        local = new QNetworkAddressEntry();
        local->setIp(QHostAddress(localIp));
    }

    if (local != nullptr)
    {
        bool sub = local->ip().isInSubnet(QHostAddress::parseSubnet("192.168.0.0/16"));
        upnpDis = new UPNPConnection(*local);
        upnpNet = new UPNPConnection(*local);
        qDebug() << "Sub:" << sub;

        if (sub)
        {
            // connect(upnpNet, &UPNPConnection::success, this, &NetworkManager::startNetwork);
            // connect(upnpDis, &UPNPConnection::success, this, &NetworkManager::startDiscovery);
            connect(upnpNet, &UPNPConnection::upnpError,
                    [](QString msg) { qDebug() << "[NetworkManager] UPnP error:" << msg; });
            connect(upnpDis, &UPNPConnection::upnpError,
                    [](QString msg) { qDebug() << "[NetworkManager] UPnP Discovery error:" << msg; });
            // qDebug() << "Tunnel creation started!";
            // upnpDis->makeTunnel(extPort, extPort, " UDP ", "Discovery tunnel of ExtraChain ");
            // upnpNet->makeTunnel(tcpPort, tcpPort, "TCP", "Network tunnel of ExtraChain ");
        }
        else
        {
            // startDiscovery();
        }
    }
    else
    {
        qDebug() << "Local not found";
    }
}

void NetworkManager::process()
{
    startNetwork();

    auto tempTimer = new QTimer(this);
    connect(tempTimer, &QTimer::timeout, [this] { this->reconnection(); });
    tempTimer->start(5000);
}

void NetworkManager::reconnection()
{
    for (const auto &el : qAsConst(m_reconnections))
    {
        bool finded = std::find_if(m_connections.begin(), m_connections.end(),
                                   [el](SocketService *service) { return service->ip() == el.first; })
            != m_connections.end();

        if (finded)
            continue;

        qDebug().noquote() << "[NetworkManager]" << (tcpPort == 2222 ? "Node:" : "DFS:") << "Reconnection to"
                           << el.first << el.second;
        connectToNode(el.first, el.second);
    }
}

void NetworkManager::connectTcpSocket(TcpSocketService *service)
{
    connect(service, &TcpSocketService::error, this, &NetworkManager::socketError);
    connect(service, &TcpSocketService::disconnected, this, &NetworkManager::removeTcpConnection);
    connect(service, &TcpSocketService::activated, this, &NetworkManager::checkConnectionsStatus);

    if (!m_connections.contains(service))
        m_connections.append(service);
}

void NetworkManager::connectWsService(WebSocketService *service)
{
    connect(service, &WebSocketService::error, this, &NetworkManager::socketError);
    connect(service, &WebSocketService::disconnected, this, &NetworkManager::removeWsConnection);
    connect(service, &TcpSocketService::activated, this, &NetworkManager::checkConnectionsStatus);

    if (!m_connections.contains(service))
        m_connections.append(service);
}

void NetworkManager::removeConnection(const QString &identifier)
{
    if (identifier.isEmpty())
        qFatal("Try remove with empty identifier");

    for (auto connection : qAsConst(m_connections))
    {
        if (connection->identifier() == identifier)
            emit connection->close();
    }
}

NetworkManager::~NetworkManager()
{
    // delete resolverService;
    delete upnpNet;
    delete upnpDis;
    delete local;
    delete tcpServer;
    // delete discoveryService;

    for (const auto &connection : qAsConst(m_connections))
    {
        emit connection->close();
        emit connection->finished();
    }
    m_connections.clear();
}

void NetworkManager::checkConnectionsStatus()
{
    bool flag = false;
    int count = 0;
    std::for_each(m_connections.begin(), m_connections.end(), [&flag, &count](SocketService *el) {
        flag = flag || el->isActive();
        if (el->isActive())
            count++;
    });
    emit connectionStatusChanged(flag);
    emit connectionsCountChanged(count); // TODO: check prev count value

    if (flag) // TODO: replace to networkStatusChanged slot
        sendFromCache();
}

void NetworkManager::startNetwork()
{
    qDebug() << "[NetworkManager] Start servers...";

    if (local == nullptr)
    {
        qDebug() << "[NetworkManager] Can't detect local ip";
        return;
    }

    // temp disable for clients
#ifdef ECONSOLE
    tcpServer = new TcpServerService(tcpPort, local);
    connect(tcpServer, &TcpServerService::newServerConnection, this,
            &NetworkManager::addTcpConnectionFromServer, Qt::UniqueConnection);
    // connect(serverService, &TcpServerService::serverStatus, this, &NetworkManager::networkErrorChanged);
    if (tcpServer->startListen()) { }

    wsServer = new QWebSocketServer("ExtraChain", QWebSocketServer::SslMode::NonSecureMode);

    if (wsServer->listen(QHostAddress::Any, wsPort))
    {
        connect(wsServer, &QWebSocketServer::newConnection, this, &NetworkManager::onNewWSConnection);
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
#endif
}

void NetworkManager::startDiscovery()
{
    qDebug() << "NetworkManager::startDiscovery()";
    // discoveryService = new DiscoveryService(extPort, tcpPort, local);
    // ThreadPool::addThread(discoveryService);
    // connect(discoveryService, &DiscoveryService::ClientDiscovered, this,
    // &NetworkManager::addConnectionFromPair);
}

void NetworkManager::connectToNode(const QString &ip, Network::Protocol protocol)
{
    if (m_connections.length() >= SIZE_OF_CONNECTIONS)
    {
        qDebug() << "[NetworkManager] Can't connect because the maximum number of connections";
        return;
    }

    if (ip.isEmpty())
        return;

    qDebug().noquote().nospace() << "[NetworkManager] Connect to " << ip << ", protocol: " << protocol
                                 << ", port: " << (protocol == Network::Protocol::Tcp ? tcpPort : wsPort);
    m_reconnections << std::pair { ip, protocol };

    using Network::Protocol;
    switch (protocol)
    {
    case Protocol::Tcp:
        connectToTcpSocket(ip.simplified(), tcpPort);
        break;
    case Protocol::WebSocket:
        connectToWebSocket(ip.simplified(), wsPort);
        break;
    case Protocol::Undefined:
        qFatal("Undefined connectToNode");
    }
}

void NetworkManager::connectToWebSocket(const QString &ip, quint16 port)
{
    auto service = new WebSocketService(nullptr, this);
    service->open(QUrl(QString("ws://%1:%2").arg(ip).arg(port)));
    connectWsService(service);
}

void NetworkManager::sendMessage(const QByteArray &message, const unsigned int &msgType,
                                 const SocketPair &receiver, Config::Net::TypeSend typeSend)
{
    Config::Net::TypeSend send;

    if (typeSend == Config::Net::TypeSend::Default)
    {
        if (Messages::isChainMessage(msgType) || Messages::isGeneralRequest(msgType) || msgType == 400
            || msgType == 402)
            send = Config::Net::TypeSend::All;
        else if (Messages::isGeneralResponse(msgType) || msgType == 401 || msgType == 403)
            send = Config::Net::TypeSend::Focused;
        else
            send = Config::Net::TypeSend::Except;
    }
    else
    {
        send = typeSend;
    }

    auto allActive = [this] {
        if (m_connections.isEmpty())
            return false;

        for (const auto &el : qAsConst(m_connections))
        {
            if (el->isActive())
                return true;
        }

        return false;
    };

    if (!allActive())
        saveToCache(message, msgType, receiver, send); // TODO: check ws

    auto isSendCheck = [](Config::Net::TypeSend send, std::string_view socketIp, quint16 socketPort,
                          const SocketPair &pair) {
        switch (send)
        {
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

    for (const auto &service : qAsConst(m_connections))
    {
        bool isSend = isSendCheck(send, service->ip().toStdString(), service->port(), receiver);
        if (!isSend)
            continue;
        if (service->isActive())
            emit service->send(message);
    }
}

bool NetworkManager::checkMsgCount(const QByteArray &msg)
{
    bool flag_result = true;
    bool value = 0;
    QByteArray hashMsg = Utils::calcKeccak(msg);
    QMap<QByteArray, int>::iterator it = msgHashList.find(hashMsg);
    if (it == msgHashList.end())
        msgHashList.insert(hashMsg, value);
    else
    {
        if (msgHashList.find(hashMsg).value() == m_connections.length() - 1)
        {
            msgHashList.remove(hashMsg);
            flag_result = false;
        }
        else
        {
            msgHashList.find(hashMsg).value()++;
            flag_result = true;
        }
    }
    return flag_result;
}

void NetworkManager::saveToCache(const QByteArray &message, const unsigned int &msgType,
                                 const SocketPair &receiver, Config::Net::TypeSend typeSend)
{
    QFile file("tmp/network.cache");
    file.open(QFile::Append);
    QByteArrayList list = { message,
                            QByteArray::fromStdString(receiver.ip),
                            QByteArray::number(receiver.port),
                            receiver.m_identifier,
                            QByteArray::number(msgType),
                            QByteArray::number(typeSend) };
    QByteArray package = Serialization::serialize(list, 8);
    file.write(Utils::intToByteArray(package.length(), 8) + package);
    file.close();
}

void NetworkManager::sendFromCache()
{
    QFile file("tmp/network.cache");
    if (!file.exists())
        return;
    if (!file.open(QFile::ReadOnly))
        return;
    QByteArrayList allPackages = Serialization::deserialize(file.readAll(), 8);
    file.close();
    file.remove();

    for (const QByteArray &packageData : qAsConst(allPackages))
    {
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
        sendMessage(data, msgType, socketData, typeSend);
    }
}

void NetworkManager::messageReceived(const QByteArray &msg, const SocketPair &receiver)
{
    if (checkMsgCount(msg))
        resolveManager->setTask(msg, receiver);
    else
        qDebug()
            << "[Network Manager] checkMsgCount have returned false: such message has been already added";
}

void NetworkManager::connectToTcpSocket(const QString &ip, quint16 port)
{
    TcpSocketService *socket = new TcpSocketService(ip, this);
    connectTcpSocket(socket);
    qDebug().noquote().nospace() << "[NetworkManager] New TCP connection: " << ip << ":" << port;
    ThreadPool::addThread(socket);
}

void NetworkManager::addTcpConnectionFromServer(qint64 socketDescriptor)
{
    if (m_connections.length() >= SIZE_OF_CONNECTIONS)
    {
        qDebug() << "[NetworkManager] Can't connect from tcp server because the maximum number of connections"
                 << tcpPort << wsPort;
        return;
    }

    TcpSocketService *socket = new TcpSocketService(socketDescriptor, this);
    connectTcpSocket(socket);
    // QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    ThreadPool::addThread(socket);
}

void NetworkManager::removeTcpConnection() //
{
    QObject *sender = QObject::sender();

    if (sender == nullptr)
        return;

    TcpSocketService *connection = qobject_cast<TcpSocketService *>(sender);
    auto removed = m_connections.removeAll(connection);
    if (removed == 0)
        return;
    qDebug() << "[TCP] Removed" << connection;
    emit connection->finished();
    checkConnectionsStatus();
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

void NetworkManager::socketError(Network::SocketServiceError error, QString errorData)
{
    if (QObject::sender() == nullptr)
        return;

    auto service = qobject_cast<SocketService *>(QObject::sender());
    qDebug() << "[NetworkManager] Error socket:" << error << service->identifier();

    if (error != Network::SocketServiceError::DuplicateIdentifier)
        m_reconnections.remove({ service->ip(), service->protocol() });

    if (error == Network::IncompatibleNetwork || error == Network::IncompatibleVersion)
        emit connectionError(error, service->identifier(), errorData);
}

QString NetworkManager::localIp()
{
    return local->ip().toString();
}

void NetworkManager::send(const QByteArray &data, const unsigned int &msgType, const SocketPair &receiver,
                          Config::Net::TypeSend typeSend)
{
    Messages::BaseMessage msg;
    msg.type = msgType;
    msg.data = data;
    QByteArray message = msg.serialize();
    sendMessage(message, msgType, receiver, typeSend);
}

void NetworkManager::onNewWSConnection()
{
    auto ws = wsServer->nextPendingConnection();
    if (ws == nullptr)
        qFatal("[WS] Error: ws == nulltpr");

    if (m_connections.length() >= SIZE_OF_CONNECTIONS)
    {
        qDebug() << "[NetworkManager] Can't connect from WS server because the maximum number of connections";
        return;
    }

    auto service = new WebSocketService(ws, this);
    connectWsService(service);
    emit newSocket();
}
