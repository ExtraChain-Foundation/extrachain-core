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

const QList<TcpSocketService *> &NetworkManager::getTcpConnections() const
{
    return tcpConnections;
}

const QList<WebSocketService *> &NetworkManager::getWsConnections() const
{
    return wsConnections;
}

NetworkManager *NetworkManager::getMe()
{
    return this;
}

void NetworkManager::setResolveManager(ResolveManager *value)
{
    resolveManager = value;
}

void NetworkManager::addTempConnections(const QList<QByteArray> &value)
{
    tempConnections += value;
}

NetworkManager::NetworkManager(AccountController *accountList, ActorIndex *actorIndex, const QString &localIp)
{
    requestResponseMap = new QMap<QByteArray, int>();

    // deviceId = BigNumber(Network::currentIdentifier());
    accounts = accountList;
    this->actorIndex = actorIndex;
    // setupActorIndexConnections();

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

    qDebug() << "[NetworkManager] init net fun start" << (local != nullptr);

    if (local != nullptr)
    {
        // qDebug() << "LOCAL ::::::::::::::::" << local->ip();
        bool sub = local->ip().isInSubnet(QHostAddress::parseSubnet("192.168.0.0/16"));
        upnpDis = new UPNPConnection(*local);
        upnpNet = new UPNPConnection(*local);
        qDebug() << "Sub:" << sub;
        if (sub)
        {

            startDiscovery();
            //            QObject::connect(upnpNet, SIGNAL(success()), this,
            //            SLOT(startNetwork())); QObject::connect(upnpDis,
            //            SIGNAL(success()), this, SLOT(startDiscovery()));
            //            connect(upnpNet, SIGNAL(upnp_error(QString)), this,
            //            SLOT(upnpErrNet(QString))); connect(upnpDis,
            //            SIGNAL(upnp_error(QString)), this,
            //            SLOT(upnpErrDis(QString))); qDebug() << "Tunnel creation
            //            started!"; upnpDis->makeTunnel(extPort, extPort, "UDP",
            //                                "Discovery tunnel of ExtraChain ");
            //            upnpNet->makeTunnel(netPort, netPort, "TCP",
            //                                "Network tunnel of ExtraChain ");
        }
        else
        {
            startDiscovery();
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
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &NetworkManager::checkConnectionsStatus);
    timer->start(5000);
}

void NetworkManager::showMessage(const QHostAddress &from, const QString &message)
{
    qDebug() << from.toIPv4Address() << " " << message;
}

void NetworkManager::resolverMessage(const QHostAddress &from, const QString &message)
{
    qDebug() << from.toIPv4Address() << " " << message;
}

void NetworkManager::connectTcpSocket(TcpSocketService *service)
{
    // connect(this, &NetworkManager::sendMsg, service, &SocketService::sendMsg);
    // connect(service, &SocketService::MessageReceived, this, &NetworkManager::MessageReceived);
    connect(service, &TcpSocketService::close, this, &NetworkManager::removeTcpConnection);
    connect(service, &TcpSocketService::checkMe, this, &NetworkManager::checkMyIdentifier);
    // connect(service, &SocketService::moveMe, this, &NetworkManager::MoveToDfsN);
}

void NetworkManager::disconnectTcpSocket(TcpSocketService *socket)
{
    //    disconnect(socket, &TcpSocketService::clientRemove, this, &NetworkManager::removeTcpConnection);
    //    disconnect(this, &NetworkManager::sendMsg, socket, &SocketService::sendMsg);
    //    disconnect(socket, &TcpSocketService::clientDisconnected, this,
    //    &NetworkManager::removeTcpConnection); disconnect(socket, &SocketService::MessageReceived, this,
    //    &NetworkManager::MessageReceived); disconnect(socket, &SocketService::moveMe, this,
    //    &NetworkManager::MoveToDfsN);
}

void NetworkManager::connectWsService(WebSocketService *service)
{
    //    connect(service, &WebSocketService::resolveMessage,
    //            [this](QByteArray msg, SocketPair receiver) { MessageReceived(msg, receiver); });
    connect(service, &WebSocketService::error, this, &NetworkManager::webSocketError);
    connect(service, &WebSocketService::disconnected, this, &NetworkManager::removeWsConnection);

    if (!wsConnections.contains(service))
        wsConnections.append(service);
}

void NetworkManager::removeConnection(const QString &identifier)
{
    if (identifier.isEmpty())
        qFatal("Try remove with empty identifier");

    auto searchAndRemove = [identifier](const auto &list) {
        for (auto connection : qAsConst(list))
        {
            if (connection->identifier() == identifier)
                emit connection->close();
        }
    };

    searchAndRemove(tcpConnections);
    searchAndRemove(wsConnections);
}

TcpSocketService *NetworkManager::getConnectionByAddress(const QByteArray address) const
{
    for (const auto currentConnection : tcpConnections)
    {
        if (currentConnection == nullptr)
            continue;
        if (currentConnection->ip() == address)
            return currentConnection;
    }
    qFatal("TcpSocketService == nullptr");
    return nullptr;
}

int NetworkManager::connectionsCount() const
{
    return tcpConnections.length() + wsConnections.length();
}

NetworkManager::~NetworkManager()
{
    // delete resolverService;
    delete upnpNet;
    delete upnpDis;
    delete local;
    delete serverService;
    // delete discoveryService;
    for (auto delSock : qAsConst(tcpConnections))
    {
        // delSock->get
        delSock->socket()->disconnectFromHost();
        delete delSock;
    }
    // TODO: remove WS
    // qDeleteAll(wsList.begin(), wsList.end());
    emit finished();
}

void NetworkManager::checkConnectionsStatus()
{
    bool flag = false;
    std::for_each(tcpConnections.begin(), tcpConnections.end(),
                  [&flag](TcpSocketService *el) { flag = flag || el->isActive(); });
    std::for_each(wsConnections.begin(), wsConnections.end(),
                  [&flag](WebSocketService *el) { flag = flag || el->isActive(); });
    emit networkStatusChanged(flag);
    emit networkSocketsCountChanged(connectionsCount());

    if (flag)
        sendFromCache();
}

void NetworkManager::checkMyIdentifier()
{
    QObject *sender = QObject::sender();
    TcpSocketService *connection = qobject_cast<TcpSocketService *>(sender);
    bool removed = false;

    if (connection == nullptr)
        return;

    if (Network::currentIdentifier() == connection->identifier())
    {
        emit connection->close();
        removed = true;
    }

    // short counter = 0;
    for (TcpSocketService *el : qAsConst(tcpConnections))
    {
        if (el->identifier() == connection->identifier() && el != connection)
        {
            emit el->close();
            // return;
        }
    }

    if (removed)
    {
        return;
    }

    emit connection->setActiveSignal(true);
    emit newSocket();
    //    std::for_each(connections.begin(), connections.end(), [connection](SocketService *el) {
    //        if (el->getIdentifier() == connection->getIdentifier())
    //        {
    //            if (el == connection)
    //            {
    //                emit el->setActiveSignal(true);
    //            }
    //            else
    //                emit el->removeMe();
    //        }
    //    });

    // if (counter == 0)
    //    emit connection->setActiveSignal(true);
}

void NetworkManager::startNetwork()
{
    qDebug() << "[NetworkManager] Start servers...";

    if (local == nullptr)
    {
        qDebug() << "[NetworkManager] Can't detect local ip";
#ifdef ECONSOLE
        qFatal("[NetworkManager] Emptry local ip");
#endif
        return;
    }

    serverService = new TcpServerService(tcpPort, local);
    setupServerServiceConnections();
    serverService->startListen();

#ifdef ECONSOLE
    wsServer = new QWebSocketServer(QStringLiteral("ExtraChain %1").arg(EXTRACHAIN_VERSION),
                                    QWebSocketServer::SslMode::NonSecureMode);

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
    setupDiscoveryServiceConnections();
}

void NetworkManager::connectToNode(const QString &ip, Network::Protocol protocol)
{
    if (connectionsCount() >= SIZE_OF_CONNECTIONS)
    {
        qDebug() << "[NetworkManager] Can't connect because the maximum number of connections";
        return;
    }

    if (ip.isEmpty())
        return;

    qDebug().noquote() << QString("[NetworkManager] Try connect to %1, protocol: %2, port: %3")
                              .arg(ip)
                              .arg(int(protocol))
                              .arg(protocol == Network::Protocol::Tcp ? tcpPort : wsPort);

    using Network::Protocol;
    switch (protocol)
    {
    case Protocol::Tcp:
        connectToTcpSocket(ip.simplified(), tcpPort);
        break;
    case Protocol::WebSocket:
        connectToWebSocket(ip.simplified(), wsPort);
        break;
    }
}

void NetworkManager::connectToWebSocket(const QString &ip, quint16 port)
{
    auto service = new WebSocketService(nullptr, this, actorIndex);
    service->open(QUrl(QString("ws://%1:%2").arg(ip).arg(port)));
    connectWsService(service);
}

void NetworkManager::setupServerServiceConnections()
{
    connect(serverService, &TcpServerService::newServerConnection, this,
            &NetworkManager::addTcpConnectionFromServer, Qt::UniqueConnection);
    connect(serverService, &TcpServerService::serverStatus, this, &NetworkManager::networkErrorChanged);
}

void NetworkManager::setupDiscoveryServiceConnections()
{
    //    connect(discoveryService, &DiscoveryService::ClientDiscovered, this,
    //            &NetworkManager::addConnectionFromPair);
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
        if (tcpConnections.isEmpty() && wsConnections.isEmpty())
            return false;

        for (const auto &tmp : qAsConst(tcpConnections))
            if (tmp->isActive())
                return true;
        for (const auto &tmp : qAsConst(wsConnections))
            if (tmp->isActive())
                return true;

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

    for (const auto &tcp : qAsConst(tcpConnections))
    {
        bool isSend = isSendCheck(send, tcp->ip().toStdString(), tcp->port(), receiver);
        if (!isSend)
            continue;
        if (tcp->isActive())
            emit tcp->send(message);
    }

    for (const auto &ws : qAsConst(wsConnections))
    {
        bool isSend = isSendCheck(send, ws->ip().toStdString(), ws->port(), receiver);
        if (!isSend)
            continue;
        if (ws->isActive())
            emit ws->send(message);
    }
}

bool NetworkManager::checkMsgCount(const QByteArray &msg, QMap<QByteArray, int> &handler,
                                   const QList<TcpSocketService *> list)
{
    bool flag_result = true;
    bool value = 0;
    QByteArray hashMsg = Utils::calcKeccak(msg);
    QMap<QByteArray, int>::iterator it = handler.find(hashMsg);
    if (it == handler.end())
        handler.insert(hashMsg, value);
    else
    {
        if (handler.find(hashMsg).value() == connectionsCount() - 1)
        {
            handler.remove(hashMsg);
            flag_result = false;
        }
        else
        {
            handler.find(hashMsg).value()++;
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

void *NetworkManager::MessageReceived(const QByteArray &msg, const SocketPair &receiver)
{
    QMutex mutex;
    mutex.lock();
    if (checkMsgCount(msg, handler, tcpConnections))
        resolveManager->setTask(msg, receiver);
    // emit MsgReceived(msg, receiver);
    else
        qDebug()
            << "[Network Manager] checkMsgCount have returned false: such message has been already added";
    mutex.unlock();
    return nullptr;
}

void NetworkManager::upnpErrDis(QString msg)
{
    qCritical() << "NET MANAGER: UPnP Error:" << msg;
}

void NetworkManager::upnpErrNet(QString msg)
{
    qCritical() << "NET MANAGER: UPnP Error:" << msg;
}

TcpSocketService *NetworkManager::connectToTcpSocket(const QString &ip, quint16 port)
{
    TcpSocketService *socket = new TcpSocketService(ip, port);
    socket->setNetworkManager(this);
    tcpConnections.append(socket);
    connectTcpSocket(socket);
    qDebug().noquote().nospace() << "[NetworkManager] New TCP connection: " << ip << ":" << port;
    ThreadPool::addThread(socket);
    // socket->process();
    // QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    return socket;
}

void NetworkManager::addTcpConnectionFromServer(qint64 socketDescriptor)
{
    if (connectionsCount() >= SIZE_OF_CONNECTIONS)
    {
        qDebug() << "[NetworkManager] Can't connect from tcp server because the maximum number of connections"
                 << tcpPort << wsPort;
        return;
    }

    TcpSocketService *socket = new TcpSocketService(socketDescriptor);
    socket->setNetworkManager(this);
    tcpConnections.append(socket);
    connectTcpSocket(socket);
    // QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    ThreadPool::addThread(socket);
}

void NetworkManager::removeTcpConnection()
{
    QObject *sender = QObject::sender();

    if (sender == nullptr)
        return;

    TcpSocketService *connection = qobject_cast<TcpSocketService *>(sender);
    disconnectTcpSocket(connection);
    emit connection->finished();
    tcpConnections.removeAt(tcpConnections.indexOf(connection));
    checkConnectionsStatus();
}

void NetworkManager::removeWsConnection()
{
    if (QObject::sender() == nullptr)
        return;

    auto service = qobject_cast<WebSocketService *>(QObject::sender());
    wsConnections.removeAll(service);
    qDebug() << "[WS] Removed" << service;
    service->deleteLater();
}

void NetworkManager::webSocketError(Network::SocketServiceError error, QString errorData)
{
    if (QObject::sender() == nullptr)
        return;

    auto service = qobject_cast<WebSocketService *>(QObject::sender());
    qDebug() << "[NetworkManager] WS: Error socket" << int(error) << service->identifier();

    if (error == Network::IncompatibleNetwork || error == Network::IncompatibleVersion)
        emit onWebSocketError(error, service->identifier(), errorData);
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

void NetworkManager::signMessage(BaseMessage &message) const
{
    message.calcDigSig(*accounts->getMainActor());
}

QByteArray NetworkManager::calcHash(const Messages::IMessage &message) const
{
    return Utils::calcKeccak(message.serialize());
}

void NetworkManager::createNewConnectionsFromList(const QByteArray &message)
{
    Messages::ConnectionsMessage msg;
    msg = message;
    std::vector<std::pair<std::string, int>> list = msg.hosts;
    for (auto &el : list)
    {
        TcpSocketService *socket = new TcpSocketService(QString::fromStdString(el.first), el.second);
        if (tcpConnections.indexOf(socket) == -1)
        {
            tcpConnections.append(socket);
            ThreadPool::addThread(socket);
            connectTcpSocket(socket);
        }
    }
}

void NetworkManager::onNewWSConnection()
{
    auto ws = wsServer->nextPendingConnection();
    if (ws == nullptr)
        qFatal("[WS] Error: ws == nulltpr");

    if (connectionsCount() >= SIZE_OF_CONNECTIONS)
    {
        qDebug() << "[NetworkManager] Can't connect from WS server because the maximum number of connections";
        return;
    }

    auto service = new WebSocketService(ws, this, actorIndex);
    connectWsService(service);
    emit newSocket();
}

quint16 NetworkManager::getServerPort() const
{
    return tcpPort;
}

QNetworkAddressEntry *NetworkManager::getLocal() const
{
    return local;
}

QByteArray NetworkManager::getSerializedConnectionList() const
{
    QList<QByteArray> connectionsList;

    for (auto connection : this->tcpConnections)
    {
        if (!connection->isActive())
            continue;
        if (Network::currentIdentifier() == connection->identifier())
            // if it equivalent to my indetificator
            continue;
        if (connection->ip() == this->getLocal()->ip().toString().toLocal8Bit())
            // if it's my ip address
            continue;

        connectionsList.append(Serialization::serialize(
            { connection->identifier().toLatin1(), connection->ip().toLocal8Bit() }));
    }

    return Serialization::serialize(connectionsList);
}

void NetworkManager::checkOnValidConnection(QByteArray id, QByteArray address)
{
    QList<QByteArray> idAddressPair;
    for (const auto &connection : qAsConst(tempConnections))
    {
        idAddressPair = Serialization::deserialize(connection);
        if (idAddressPair.size() != 2)
        {
            qDebug() << "[Error][" << __LINE__ << "][" << __FILE__ << "]" << __FUNCTION__ << "] size!=2";
            continue;
        }
        if (idAddressPair[1] == address && idAddressPair[0] != id)
        {
            tempConnections.removeOne(connection);
            removeConnection(id);

            return;
        }
    }
}
