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

#ifndef EXTRACHAIN_CMAKE
#include "preconfig.h"
#endif

using namespace Messages;

QList<SocketService *> NetManager::getConnections() const
{
    return connections;
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
#ifdef ECLIENT
    QSettings settings;

    if (!settings.value("network/serverIp").isValid())
        settings.setValue("network/serverIp", Network::serverIp);
    if (!settings.value("network/allowLocalServer").isValid())
        settings.setValue("network/allowLocalServer", "false");

    serverIp = settings.value("network/serverIp").toString();
    allowLocalServer = settings.value("network/allowLocalServer").toBool();
#endif
    qDebug() << "Current server IPs:" << serverIp << "| allow local:" << allowLocalServer;

    //    deviceId = BigNumber(readNetManagerIdentificator());
    //    ThreadPool::addThread(this);

    this->extPort = 2223;
    this->netPort = serverPort;
    qDebug() << "NET MANAGER: netport =" << netPort << "extPort =" << extPort;

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

    connect(this, &NetManager::webSocketsCountChanged,
            [this](int count) { qDebug() << "[WS] Count:" << count << wsPort; });
}

void NetManager::process()
{
    startNetwork();
    // connectToServer(serverPort, local);
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

void NetManager::connectSocket()
{
    //    connect(this, &NetManager::sendMsg, connections.last(), &SocketService::sendMsg);
    connect(connections.last(), &SocketService::clientDisconnected, this, &NetManager::removeConnection);
    //    connect(connections.last(), &SocketService::MessageReceived, this, &NetManager::MessageReceived);
    connect(connections.last(), &SocketService::removeMe, this, &NetManager::removeConnection);
    connect(connections.last(), &SocketService::checkMe, this, &NetManager::checkMyIdentificator);
    //    connect(connections.last(), &SocketService::moveMe, this, &NetManager::MoveToDfsN);
}

void NetManager::disconnectSocket(SocketService *connection)
{
    disconnect(connection, &SocketService::clientRemove, this, &NetManager::removeConnection);
    //    disconnect(this, &NetManager::sendMsg, connection, &SocketService::sendMsg);
    disconnect(connection, &SocketService::clientDisconnected, this, &NetManager::removeConnection);
    //    disconnect(connection, &SocketService::MessageReceived, this, &NetManager::MessageReceived);
    //    disconnect(connections.last(), &SocketService::moveMe, this, &NetManager::MoveToDfsN);
}

void NetManager::connectWsService(WebSocketService *service)
{
    service->setNetworkManager(this);
    //    connect(service, &WebSocketService::resolveMessage,
    //            [this](QByteArray msg, SocketPair receiver) { MessageReceived(msg, receiver); });
    connect(service, &WebSocketService::disconnected, [this]() {
        auto service = qobject_cast<WebSocketService *>(sender());
        qDebug() << "[WS] Try remove service";

        if (service)
        {
            int count = wsConnections.length();
            wsConnections.removeAll(service);
            if (count == wsConnections.length())
                qFatal("[WS] Cant remove");
            service->deleteLater();
            qDebug() << "[WS] Removed" << service;
            emit webSocketsCountChanged(wsConnections.length());
        }
    });

    if (!wsConnections.contains(service))
    {
        wsConnections << service;
        emit webSocketsCountChanged(wsConnections.length());
    }
}

void NetManager::removeConnectionByAddress(QByteArray address)
{
    for (auto i : qAsConst(connections))
    {
        if (i->getAddress() == address)
        {
            emit i->removeMe();
            return;
        }
    }
}

SocketService NetManager::getConnectionByAddress(const QByteArray address) const
{
    for (const auto currentConnection : connections)
    {
        if (currentConnection == nullptr)
            continue;
        if (currentConnection->getAddress() == address)
            return *currentConnection;
    }
    return SocketService();
}

NetManager::~NetManager()
{
    //    delete resolverService;
    delete upnpNet;
    delete upnpDis;
    delete local;
    delete serverService;
    //    delete discoveryService;
    for (auto delSock : qAsConst(connections))
    {
        //        delSock->get
        delSock->getSocket()->disconnectFromHost();
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
    std::for_each(connections.begin(), connections.end(),
                  [&flag](SocketService *el) { flag = flag || el->getActive(); });
    std::for_each(wsConnections.begin(), wsConnections.end(),
                  [&flag](WebSocketService *el) { flag = flag || el->isActive(); });
    emit networkStatusChanged(flag);
    emit networkSocketsCountChanged(connections.length());

#ifdef ECLIENT
    if (flag)
        sendFromCache();
#endif
}

void NetManager::restoreConnections(const QList<SocketPair> &socketList)
{
    //
    for (const SocketPair &el : socketList)
    {
        addConnectionFromPair(QHostAddress(QString::fromStdString(el.ip)), el.port);
    }
}

void NetManager::checkMyIdentificator()
{
    QObject *sender = QObject::sender();
    SocketService *connection = qobject_cast<SocketService *>(sender);
    bool removed = false;

    if (connection == nullptr)
        return;

    if (allowLocalServer && net::readNetManagerIdentificator() == connection->getIdentificator())
    {
        emit connection->removeMe();
        removed = true;
    }

    // short counter = 0;
    for (SocketService *el : qAsConst(connections))
    {
        if (el->getIdentificator() == connection->getIdentificator() && el != connection)
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
    //        if (el->getIdentificator() == connection->getIdentificator())
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
    qDebug() << "startNetwork()";
    qDebug() << "NetPort:" << this->serverPort;

    if (local == nullptr)
    {
        qDebug() << "[Network] Cant detect local ip";
#ifdef ECONSOLE
        qFatal("Emptry local ip");
#endif
        return;
    }

    serverService = new ServerService(serverPort, local);
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
    netPort = serverPort;
    extPort = 2223;
    //    discoveryService = new DiscoveryService(extPort, netPort, local);
    //    ThreadPool::addThread(discoveryService);
    setupDiscoveryServiceConnections();
}

void NetManager::logDebug()
{
    qDebug() << "Networkmanager in other thread is work";
}

void NetManager::reconnectUi()
{
    if (local != nullptr)
        emit localIpFounded(local->ip().toString());
    // connectToServer(serverPort, local);
    connectToWs();
}

void NetManager::connectToServerByIpList(QList<QByteArray> ipList)
{
    QByteArrayList idIpPair;

    bool connectionIsActive;
    QByteArray currentId;
    for (const auto &ip : qAsConst(ipList))
    {
        idIpPair = Serialization::deserialize(ip);
        currentId = (getConnectionByAddress(idIpPair[1])).getID().toByteArray();
        connectionIsActive = (getConnectionByAddress(idIpPair[1])).isActive();

        if (!connectionIsActive || currentId == "0" || currentId == idIpPair[0]
            || currentId == net::readNetManagerIdentificator())
            continue;

        if (idIpPair.size() != 2)
        {
            qDebug() << "[Error][" << __LINE__ << "][" << __FILE__ << "]" << __FUNCTION__ << "] size!=2";
            continue;
        }

        addConnectionFromPair(QHostAddress(QString(idIpPair[1])), serverPort);
    }
}

void NetManager::connectToServer(const quint16 &serverPort, QNetworkAddressEntry *local)
{
#ifdef ECONSOLE
    return;
#endif
    qDebug() << "void NetManager::connectToServer()";
    QStringList servers = serverIp.split(";");
    QString localIp = local != nullptr ? local->ip().toString() : "";

    for (QString server : servers)
    {
        server = server.trimmed();
        if (server.isEmpty())
            continue;

        quint16 port = serverPort;
        bool customPort = server.indexOf(":") != -1;

        if (customPort)
        {
            QStringList serverAndPort = server.split(":");
            server = serverAndPort[0].trimmed();
            port = quint16(serverAndPort[1].trimmed().toUInt());
        }

        if (server != localIp || allowLocalServer)
        {
            qDebug().noquote() << QString("Server: try connect to %1:%2").arg(server).arg(port);
            addConnectionFromPair(QHostAddress(server), port);
        }
        else
        {
            qDebug().noquote() << QString("Server: ignore %1:%2").arg(server).arg(port);
        }
    }
}

void NetManager::connectToWs()
{
    auto service = new WebSocketService;
    service->open(QUrl(QString("ws://%1:%2").arg(serverIp).arg(wsPort)));
    connectWsService(service);
}

void NetManager::setupServerServiceConnections()
{
    connect(serverService, &ServerService::newServerConnection, this, &NetManager::addConnection,
            Qt::UniqueConnection);
    connect(serverService, &ServerService::serverStatus, this, &NetManager::networkErrorChanged);
}

void NetManager::setupDiscoveryServiceConnections()
{
    //    connect(discoveryService, &DiscoveryService::ClientDiscovered, this,
    //            &NetManager::addConnectionFromPair);
}

// Basic methods
void NetManager::broadcastMsg(const QByteArray &msg)
{
    SocketPair socketPair("0.0.0.0", 0);
    //    emit sendMsg(msg, socketPair);
    distMessage(msg, socketPair);
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

    auto allActive = [this] { //
        for (const auto &tmp : qAsConst(connections))
        {
            if (tmp->getActive())
                return true;
        }
        for (const auto &tmp : qAsConst(wsConnections))
        {
            if (tmp->isActive())
                return true;
        }

        return false;
    };

    // TODO: protocol for receiver
    if (wsConnections.isEmpty() || !allActive())
        saveToCache(message, msgType, receiver, send);

    for (const auto &tmp : qAsConst(connections))
    {
        bool isSend = false;

        switch (send)
        {
        case Config::Net::TypeSend::Except:
            isSend = tmp->getAddress().toStdString() != receiver.ip && tmp->getPort() != receiver.port;
            break;
        case Config::Net::TypeSend::Focused:
            isSend = tmp->getAddress().toStdString() == receiver.ip && tmp->getPort() == receiver.port;
            break;
        case Config::Net::TypeSend::All:
            isSend = true;
            break;
        default:
            break;
        }

        if (!isSend)
            continue;
        if (tmp->getActive())
            tmp->distMsg(message, receiver);
    }

    for (const auto &ws : qAsConst(wsConnections))
    {
        bool isSend = false;
        auto ip = ws->ip().toStdString();
        auto port = ws->ws()->localPort();

        // if (receiver.protocol != NetworkProtocol::WebSocket)
        //     qFatal("[WS] Protocol error");

        // qDebug() << "!!!!!!!!! 2 rec:" << receiver.ip.c_str() << "ip:" << ip.c_str();
        switch (send)
        {
        case Config::Net::TypeSend::Except:
            isSend = ip != receiver.ip && port != receiver.port;
            break;
        case Config::Net::TypeSend::Focused:
            isSend = ip == receiver.ip && port == receiver.port;
            break;
        case Config::Net::TypeSend::All:
            isSend = true;
            break;
        default:
            break;
        }

        if (!isSend)
            continue;
        if (ws->isActive())
            ws->send(message);
    }

    //    if (checkMsgCount(message, handler, connections))
    //        broadcastMsg(message);
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
        if (handler.find(hashMsg).value() == wsConnections.size() - 1)
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
                            receiver.iden,
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
        socketData.iden = package[3];
        // TODO: protocol
        auto msgType = package[4].toUInt();
        Config::Net::TypeSend typeSend = Config::Net::TypeSend(package[5].toInt());
        sendMessage(data, msgType, socketData, typeSend);
    }
}

void NetManager::distMessage(const QByteArray &data, const SocketPair &socketData)
{
#ifdef ECLIENT
    bool flag = false;
    std::for_each(connections.begin(), connections.end(),
                  [&flag](SocketService *el) { flag = flag || el->getActive(); });

    if (flag)
    {
#endif
        // TODO: ws
        for (int i = 0; i < connections.size(); i++)
            connections[i]->distMsg(data, socketData);
#ifdef ECLIENT
    }
    else
    {
        QFile file("network_cache");
        file.open(QFile::Append);
        QByteArrayList list = { data, QByteArray::fromStdString(socketData.ip),
                                QByteArray::number(socketData.port), socketData.iden };
        QByteArray package = Serialization::serialize(list, 8);
        file.write(Utils::intToByteArray(package.length(), 8) + package);
        file.close();
    }
#endif
}

void *NetManager::MessageReceived(const QByteArray &msg, const SocketPair &receiver)
{
    QMutex mutex;
    mutex.lock();
    if (checkMsgCount(msg, handler, connections))
        resolveManager->setTask(msg, receiver);
    // emit MsgReceived(msg, receiver);
    else
        qDebug() << "[&Net Manager]::checkMsgCount have returned false ~ such message has been already added";
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

SocketService *NetManager::addConnectionFromPair(QHostAddress address, quint16 port)
{
    SocketService *socket = new SocketService(address.toString(), port);
    socket->setNetManager(this);
    connections.append(socket);
    connectSocket();
    qDebug().noquote().nospace() << "NET MANAGER: New connection is established: " << address.toString()
                                 << ":" << port;

    ThreadPool::addThread(connections.last());
    //    connections.last()->process();
    // QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    return connections.last();
}

void NetManager::addConnection(qint64 socketDescriptor)
{
    if (connections.size() >= SIZE_OF_CONNECTIONS)
        return;
    SocketService *socket = new SocketService(socketDescriptor);
    socket->setNetManager(this);
    connections.append(socket);
    connectSocket();
    // QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    ThreadPool::addThread(connections.last());
}

void NetManager::removeConnection()
{
    QObject *sender = QObject::sender();

    if (sender == nullptr)
        return;

    SocketService *connection = qobject_cast<SocketService *>(sender);
    disconnectSocket(connection);
    emit connection->finished();
    connections.removeAt(connections.indexOf(connection));
    checkConnectionsStatus();
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
        SocketService *newSock = new SocketService(QString::fromStdString(el.first), el.second);
        if (connections.indexOf(newSock) == -1)
        {
            connections.append(newSock);
            ThreadPool::addThread(connections.last());
            connectSocket();
        }
    }
}

void NetManager::onNewWSConnection()
{
    auto ws = wsServer->nextPendingConnection();
    if (ws == nullptr)
        qFatal("[WS] error");

    auto service = new WebSocketService(ws);
    connectWsService(service);
    emit webSocketsCountChanged(wsConnections.length());
}

quint16 NetManager::getServerPort() const
{
    return serverPort;
}

QString NetManager::getServerIp() const
{
    return serverIp;
}

bool NetManager::getAllowLocalServer() const
{
    return allowLocalServer;
}

QNetworkAddressEntry *NetManager::getLocal() const
{
    return local;
}

QByteArray NetManager::getSerializedConnectionList() const
{
    QList<QByteArray> connectionsList;
    for (auto i : this->connections)
    {
        if (!i->getActive())
            continue;
        if (net::readNetManagerIdentificator()
            == i->getIdentificator().toByteArray()) // if it equivalent to my indetificator
            continue;
        if (i->getAddress() == this->getLocal()->ip().toString().toLocal8Bit()) // if it's my ip address
            continue;

        connectionsList.append(
            Serialization::serialize({ i->getID().toByteArray(), i->getAddress().toLocal8Bit() }));
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
            removeConnectionByAddress(address);

            return;
        }
    }
}
