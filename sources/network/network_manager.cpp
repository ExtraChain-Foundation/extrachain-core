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

NetManager::NetManager(AccountController *accountList, ActorIndex *actorIndex)
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
    findLocal();
    qDebug() << "NET MANAGER: init net fun start" << (local != nullptr);
    if (local != nullptr)
    {
        qDebug() << "LOCAL ::::::::::::::::" << local->ip();
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

void NetManager::removeConnectionByAddress(QByteArray address)
{
    for (auto i : connections)
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
    for (auto delSock : connections)
    {
        //        delSock->get
        delSock->getSocket()->disconnectFromHost();
        delete delSock;
    }
    if (QFile(".handlerFile").exists())
        QFile(".handlerFile").remove();
    emit finished();
}

void NetManager::findLocal()
{
    const auto allInterfaces = QNetworkInterface::allInterfaces();
    const QHostAddress &localhost = QHostAddress(QHostAddress::LocalHost);
    QList<QHostAddress> localIpNotConnect;

    for (const QNetworkInterface &interface : allInterfaces)
    {
        const auto entries = interface.addressEntries();

        for (const QNetworkAddressEntry &address : entries)
        {
            if (address.ip().protocol() == QAbstractSocket::IPv4Protocol && address.ip() != localhost)
            {
                qDebug() << "NET MANAGER: Find local ip candidate:" << address.ip().toString() << interface;
                localIpNotConnect.append(address.ip());
            }
        }
    }

    for (const QNetworkInterface &interface : allInterfaces)
    {
        const auto entries = interface.addressEntries();

        for (const QNetworkAddressEntry &entry : entries)
        {
            const auto flags = interface.flags();

            bool isLoopBack = flags.testFlag(QNetworkInterface::IsLoopBack);
            bool isPointToPoint = flags.testFlag(QNetworkInterface::IsPointToPoint);
            bool isRunning = flags.testFlag(QNetworkInterface::IsRunning);
            if (!isRunning || !interface.isValid() || isLoopBack || isPointToPoint)
                continue;

#ifdef ECONSOLE
            QTcpSocket *socket = new QTcpSocket;
            socket->bind(entry.ip());
            socket->connectToHost("8.8.8.8", 53);
            bool isConnected = socket->waitForConnected(1000);
            socket->deleteLater();
            if (!isConnected)
                continue;
#endif

            if (localIpNotConnect.contains(entry.ip()))
            {
                QString name = interface.name();

                if (name.left(2) == "vm")
                    continue;
                if (name.left(2) == "wl" || name.left(3) == "eth" || name.left(2) == "en")
                {
                    local = new QNetworkAddressEntry(entry);
                    qDebug() << this << "Discovered local:" << local->ip().toString() << interface.name();
                    return;
                }
            }
        }
    }
}

void NetManager::checkConnectionsStatus()
{
    bool flag = false;
    std::for_each(connections.begin(), connections.end(),
                  [&flag](SocketService *el) { flag = flag || el->getActive(); });
    emit qmlNetworkStatus(flag);
    emit qmlNetworkSockets(connections.length());

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

    if (connection == nullptr)
        return;

    if (allowLocalServer && net::readNetManagerIdentificator() == connection->getIdentificator())
        connection->removeMe();

    // short counter = 0;
    for (SocketService *el : connections)
    {
        if (el->getIdentificator() == connection->getIdentificator() && el != connection)
        {
            emit el->removeMe();
            // return;
        }
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

    if (local != nullptr)
    {
        serverService = new ServerService(serverPort, local);
        setupServerServiceConnections();
        serverService->startListen();
    }
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
    connectToServer(serverPort, local);
}

void NetManager::connectToServerByIpList(QList<QByteArray> ipList)
{
    QByteArrayList idIpPair;

    bool connectionIsActive;
    QByteArray currentId;
    for (auto ip : ipList)
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

void NetManager::setupServerServiceConnections()
{
    connect(serverService, &ServerService::newConnection, this, &NetManager::addConnection,
            Qt::UniqueConnection);
#ifdef ECLIENT
    connect(serverService, &ServerService::serverStatus, this, &NetManager::qmlServerError);
#endif
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

    if (connections.isEmpty()) // TODO: write type send
        saveToCache(message, msgType, receiver, send);

    for (const auto &tmp : connections)
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
        // else
        //     saveToCache(message, msgType, receiver, send);
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
        if (handler.find(hashMsg).value() == list.size() - 1)
        {
            handler.remove(hashMsg);
            flag_result = false; // FALSE !!!
        }
        else
        {
            flag_result = true;
            handler.find(hashMsg).value()++;
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

    for (QByteArray packageData : allPackages)
    {
        QByteArrayList package = Serialization::deserialize(packageData, 8);
        if (package.length() != 6)
            return;

        QByteArray data = package[0];
        SocketPair socketData;
        socketData.ip = package[1].toStdString();
        socketData.port = package[2].toShort();
        socketData.iden = package[3];
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
    mutex.lock();
    if (checkMsgCount(msg, handler, connections))
        resolveManager->setTask(msg, receiver);
    //        emit MsgReceived(msg, receiver);
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
    connections.removeAt(connections.indexOf(connection));
    connection->finished();
    checkConnectionsStatus();
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
    for (auto i : tempConnections)
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
