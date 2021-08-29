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

const QList<SocketService *> &NetManager::getTcpConnections() const
{
    return tcpConnections;
}

const QList<WebSocketService *> &NetManager::getWsConnections() const
{
    return wsConnections;
}

NetManager *NetManager::getMe()
{
    return this;
}

void NetManager::setResolveManager(ResolveManager *value)
{
    resolveManager = value;
}

void NetManager::addTempConnections(const QList<QByteArray> &value)
{
    tempConnections += value;
}

NetManager::NetManager(AccountController *accountList, ActorIndex *actorIndex, const QString &localIp)
{
    requestResponseMap = new QMap<QByteArray, int>();

    // deviceId = BigNumber(readNetManagerIdentifier());
    accounts = accountList;
    this->actorIndex = actorIndex;
    // setupActorIndexConnections();

    if (localIp.isEmpty())
    {
        local = new QNetworkAddressEntry(Utils::findLocalIp(Utils::PrintDebug::Off));
        qDebug().noquote() << "[NetManager] Found local IP:" << local->ip().toString();
    }
    else
    {
        qDebug().noquote() << "[NetManager] Set local IP from settings:" << localIp;
        local = new QNetworkAddressEntry();
        local->setIp(QHostAddress(localIp));
    }

    qDebug() << "NET MANAGER: init net fun start" << (local != nullptr);
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

    connect(this, &NetManager::webSocketsCountChanged, [this](int count) {
        qDebug() << "[WS]" << wsConnections << wsConnections.length() << "Count:" << count << wsPort;
    });
}

void NetManager::process()
{
    startNetwork();
    QTimer *timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &NetManager::checkConnectionsStatus);
    timer->start(5000);
}

void NetManager::showMessage(const QHostAddress &from, const QString &message)
{
    qDebug() << from.toIPv4Address() << " " << message;
}

void NetManager::resolverMessage(const QHostAddress &from, const QString &message)
{
    qDebug() << from.toIPv4Address() << " " << message;
}

void NetManager::connectTcpSocket(SocketService *service)
{
    // connect(this, &NetManager::sendMsg, service, &SocketService::sendMsg);
    connect(service, &SocketService::clientDisconnected, this, &NetManager::removeTcpConnection);
    // connect(service, &SocketService::MessageReceived, this, &NetManager::MessageReceived);
    connect(service, &SocketService::removeMe, this, &NetManager::removeTcpConnection);
    connect(service, &SocketService::checkMe, this, &NetManager::checkMyIdentifier);
    // connect(service, &SocketService::moveMe, this, &NetManager::MoveToDfsN);
}

void NetManager::disconnectTcpSocket(SocketService *socket)
{
    disconnect(socket, &SocketService::clientRemove, this, &NetManager::removeTcpConnection);
    //    disconnect(this, &NetManager::sendMsg, socket, &SocketService::sendMsg);
    disconnect(socket, &SocketService::clientDisconnected, this, &NetManager::removeTcpConnection);
    //    disconnect(socket, &SocketService::MessageReceived, this, &NetManager::MessageReceived);
    //    disconnect(socket, &SocketService::moveMe, this, &NetManager::MoveToDfsN);
}

void NetManager::connectWsService(WebSocketService *service)
{
    service->setAbilities(this, actorIndex);
    //    connect(service, &WebSocketService::resolveMessage,
    //            [this](QByteArray msg, SocketPair receiver) { MessageReceived(msg, receiver); });
    connect(service, &WebSocketService::disconnected, this, &NetManager::removeWsConnection);

    if (!wsConnections.contains(service))
    {
        wsConnections << service;
        emit webSocketsCountChanged(wsConnections.length());
    }
}

void NetManager::removeConnection(const QString &ip, quint16 port, Network::Protocol protocol)
{
    if (protocol == Network::Protocol::Tcp)
    {
        for (auto connection : qAsConst(tcpConnections))
        {
            if (connection->ip() == ip) // TODO: add port
                emit connection->removeMe();
        }
    }
    else if (protocol == Network::Protocol::WebSocket)
    {
        for (auto connection : qAsConst(wsConnections))
        {
            if (connection->ip() == ip && (port == 0 || connection->port() == port))
                emit connection->close();
        }
    }
}

SocketService NetManager::getConnectionByAddress(const QByteArray address) const
{
    for (const auto currentConnection : tcpConnections)
    {
        if (currentConnection == nullptr)
            continue;
        if (currentConnection->ip() == address)
            return *currentConnection;
    }
    return SocketService();
}

int NetManager::connectionsCount()
{
    return tcpConnections.length() + wsConnections.length();
}

NetManager::~NetManager()
{
    //    delete resolverService;
    delete upnpNet;
    delete upnpDis;
    delete local;
    delete serverService;
    //    delete discoveryService;
    for (auto delSock : qAsConst(tcpConnections))
    {
        //        delSock->get
        delSock->socket()->disconnectFromHost();
        delete delSock;
    }
    // qDeleteAll(wsList.begin(), wsList.end());
    if (QFile(".handlerFile").exists())
        QFile(".handlerFile").remove();
    emit finished();
}

void NetManager::checkConnectionsStatus()
{
    bool flag = false;
    std::for_each(tcpConnections.begin(), tcpConnections.end(),
                  [&flag](SocketService *el) { flag = flag || el->getActive(); });
    std::for_each(wsConnections.begin(), wsConnections.end(),
                  [&flag](WebSocketService *el) { flag = flag || el->isActive(); });
    emit networkStatusChanged(flag);
    emit networkSocketsCountChanged(tcpConnections.length());

    if (flag)
        sendFromCache();
}

void NetManager::checkMyIdentifier()
{
    QObject *sender = QObject::sender();
    SocketService *connection = qobject_cast<SocketService *>(sender);
    bool removed = false;

    if (connection == nullptr)
        return;

    if (net::readNetManagerIdentifier() == connection->identifier())
    {
        emit connection->removeMe();
        removed = true;
    }

    // short counter = 0;
    for (SocketService *el : qAsConst(tcpConnections))
    {
        if (el->identifier() == connection->identifier() && el != connection)
        {
            emit el->removeMe();
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

void NetManager::startNetwork()
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

    serverService = new ServerService(tcpPort, local);
    setupServerServiceConnections();
    serverService->startListen();

#ifdef ECONSOLE
    wsServer = new QWebSocketServer(QStringLiteral("ExtraChain %1").arg(EXTRACHAIN_VERSION),
                                    QWebSocketServer::SslMode::NonSecureMode);

    if (wsServer->listen(QHostAddress::Any, wsPort))
    {
        connect(wsServer, &QWebSocketServer::newConnection, this, &NetManager::onNewWSConnection);
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

void NetManager::startDiscovery()
{
    qDebug() << "NetManager::startDiscovery()";
    // discoveryService = new DiscoveryService(extPort, tcpPort, local);
    // ThreadPool::addThread(discoveryService);
    setupDiscoveryServiceConnections();
}

void NetManager::connectToServerByIpList(QList<QByteArray> ipList)
{
    QByteArrayList idIpPair;

    bool connectionIsActive;
    QByteArray currentId;
    for (const auto &ip : qAsConst(ipList))
    {
        idIpPair = Serialization::deserialize(ip);
        currentId = (getConnectionByAddress(idIpPair[1])).identifier().toByteArray();
        connectionIsActive = (getConnectionByAddress(idIpPair[1])).isActive();

        if (!connectionIsActive || currentId == "0" || currentId == idIpPair[0]
            || currentId == net::readNetManagerIdentifier())
            continue;

        if (idIpPair.size() != 2)
        {
            qDebug() << "[Error][" << __LINE__ << "][" << __FILE__ << "]" << __FUNCTION__ << "] size!=2";
            continue;
        }

        connectToTcpSocket(idIpPair[1], tcpPort);
    }
}

void NetManager::connectToNode(const QString &ip, Network::Protocol protocol)
{
    if (connectionsCount() >= SIZE_OF_CONNECTIONS)
    {
        qDebug() << "[NetworkManager] Can't connect because the maximum number of connections";
        return;
    }

    if (ip.isEmpty())
        return;

    qDebug().noquote() << QString("[%3NetworkManager] Try connect to %1, protocol: %2")
                              .arg(ip)
                              .arg(int(protocol))
                              .arg(tcpPort == 2222 ? "" : "DFS ");

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

void NetManager::connectToWebSocket(const QString &ip, quint16 port)
{
    auto service = new WebSocketService;
    service->open(QUrl(QString("ws://%1:%2").arg(ip).arg(port)));
    connectWsService(service);
}

void NetManager::setupServerServiceConnections()
{
    connect(serverService, &ServerService::newServerConnection, this, &NetManager::addTcpConnectionFromServer,
            Qt::UniqueConnection);
    connect(serverService, &ServerService::serverStatus, this, &NetManager::networkErrorChanged);
}

void NetManager::setupDiscoveryServiceConnections()
{
    //    connect(discoveryService, &DiscoveryService::ClientDiscovered, this,
    //            &NetManager::addConnectionFromPair);
}

void NetManager::sendMessage(const QByteArray &message, const unsigned int &msgType,
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
            if (tmp->getActive())
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
        if (tcp->getActive())
            tcp->distMsg(message, receiver);
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

bool NetManager::checkMsgCount(const QByteArray &msg, QMap<QByteArray, int> &handler,
                               const QList<SocketService *> list)
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

void NetManager::saveToCache(const QByteArray &message, const unsigned int &msgType,
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

void NetManager::sendFromCache()
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

void *NetManager::MessageReceived(const QByteArray &msg, const SocketPair &receiver)
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

void NetManager::upnpErrDis(QString msg)
{
    qCritical() << "NET MANAGER: UPnP Error:" << msg;
}

void NetManager::upnpErrNet(QString msg)
{
    qCritical() << "NET MANAGER: UPnP Error:" << msg;
}

SocketService *NetManager::connectToTcpSocket(const QString &ip, quint16 port)
{
    SocketService *socket = new SocketService(ip, port);
    socket->setNetManager(this);
    tcpConnections.append(socket);
    connectTcpSocket(socket);
    qDebug().noquote().nospace() << "[NetworkManager] New TCP connection: " << ip << ":" << port;
    ThreadPool::addThread(socket);
    // socket->process();
    // QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    return socket;
}

void NetManager::addTcpConnectionFromServer(qint64 socketDescriptor)
{
    if (connectionsCount() >= SIZE_OF_CONNECTIONS)
    {
        qDebug()
            << "[NetworkManager] Can't connect from tcp server because the maximum number of connections";
        return;
    }

    SocketService *socket = new SocketService(socketDescriptor);
    socket->setNetManager(this);
    tcpConnections.append(socket);
    connectTcpSocket(socket);
    // QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    ThreadPool::addThread(socket);
}

void NetManager::removeTcpConnection()
{
    QObject *sender = QObject::sender();

    if (sender == nullptr)
        return;

    SocketService *connection = qobject_cast<SocketService *>(sender);
    disconnectTcpSocket(connection);
    emit connection->finished();
    tcpConnections.removeAt(tcpConnections.indexOf(connection));
    checkConnectionsStatus();
}

void NetManager::removeWsConnection()
{
    if (QObject::sender() == nullptr)
        return;

    auto service = qobject_cast<WebSocketService *>(QObject::sender());
    int remove = wsConnections.removeAll(service);
    qDebug() << "[WS] Removed" << service;
    service->deleteLater();
    if (remove > 0)
        emit webSocketsCountChanged(wsConnections.length());
}

QString NetManager::localIp()
{
    return local->ip().toString();
}

void NetManager::send(const QByteArray &data, const unsigned int &msgType, const SocketPair &receiver,
                      Config::Net::TypeSend typeSend)
{
    Messages::BaseMessage msg;
    msg.type = msgType;
    msg.data = data;
    QByteArray message = msg.serialize();
    sendMessage(message, msgType, receiver, typeSend);
}

void NetManager::signMessage(BaseMessage &message) const
{
    message.calcDigSig(*accounts->getMainActor());
}

QByteArray NetManager::calcHash(const Messages::IMessage &message) const
{
    return Utils::calcKeccak(message.serialize());
}

void NetManager::createNewConnectionsFromList(const QByteArray &message)
{
    Messages::ConnectionsMessage msg;
    msg = message;
    std::vector<std::pair<std::string, int>> list = msg.hosts;
    for (auto &el : list)
    {
        SocketService *socket = new SocketService(QString::fromStdString(el.first), el.second);
        if (tcpConnections.indexOf(socket) == -1)
        {
            tcpConnections.append(socket);
            ThreadPool::addThread(socket);
            connectTcpSocket(socket);
        }
    }
}

void NetManager::onNewWSConnection()
{
    auto ws = wsServer->nextPendingConnection();
    if (ws == nullptr)
        qFatal("[WS] Error: ws == nulltpr");

    if (connectionsCount() >= SIZE_OF_CONNECTIONS)
    {
        qDebug() << "[NetworkManager] Can't connect from WS server because the maximum number of connections";
        return;
    }

    auto service = new WebSocketService(ws);
    connectWsService(service);
    emit webSocketsCountChanged(wsConnections.length());
    emit newSocket();
}

quint16 NetManager::getServerPort() const
{
    return tcpPort;
}

QNetworkAddressEntry *NetManager::getLocal() const
{
    return local;
}

QByteArray NetManager::getSerializedConnectionList() const
{
    QList<QByteArray> connectionsList;
    for (auto i : this->tcpConnections)
    {
        if (!i->getActive())
            continue;
        if (net::readNetManagerIdentifier()
            == i->identifier().toByteArray()) // if it equivalent to my indetificator
            continue;
        if (i->ip() == this->getLocal()->ip().toString().toLocal8Bit()) // if it's my ip address
            continue;

        connectionsList.append(
            Serialization::serialize({ i->identifier().toByteArray(), i->ip().toLocal8Bit() }));
    }
    return Serialization::serialize(connectionsList);
}

void NetManager::checkOnValidConnection(QByteArray id, QByteArray address)
{
    QList<QByteArray> idAddressPair;
    for (const auto &i : qAsConst(tempConnections))
    {
        idAddressPair = Serialization::deserialize(i);
        if (idAddressPair.size() != 2)
        {
            qDebug() << "[Error][" << __LINE__ << "][" << __FILE__ << "]" << __FUNCTION__ << "] size!=2";
            continue;
        }
        if (idAddressPair[1] == address && idAddressPair[0] != id)
        {
            tempConnections.removeOne(i);
            removeConnection(address, 0, Network::Protocol::Tcp);

            return;
        }
    }
}
