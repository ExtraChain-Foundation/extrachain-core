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
#ifdef ETALONIUM_CLIENT
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
        qDebug() << "Sub: " << sub;
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
            //                                "Discovery tunnel of ExtraCoin ");
            //            upnpNet->makeTunnel(netPort, netPort, "TCP",
            //                                "Network tunnel of ExtraCoin ");
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
                qDebug() << "NET MANAGER: local ip: " << address.ip().toString() << " " << interface;
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

            QTcpSocket *socket = new QTcpSocket;
            socket->bind(entry.ip());
            socket->connectToHost("8.8.8.8", 53);
            bool isConnected = socket->waitForConnected(1000);
            socket->deleteLater();
            if (!isConnected)
                continue;

            if (localIpNotConnect.contains(entry.ip()))
            {
                local = new QNetworkAddressEntry(entry);
                qDebug() << "Discovered local:" << local->ip().toString();

                if (interface.name().left(2) == "wl" || interface.name().left(3) == "eth")
                {
                    qDebug() << "done";
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

#ifdef ETALONIUM_CLIENT
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
    connectToServer(serverPort, local);
}

void NetManager::connectToServerByIpList(QList<QByteArray> ipList)
{
    QByteArrayList idIpPair;

    for (auto ip : ipList)
    {
        idIpPair = Serialization::universalDeserialize(ip);
        QString ipStr = QString(idIpPair[1]);

        bool flag = false;
        for (const auto connection : connections)
        {
            if (connection == nullptr)
                continue;
            QString socketIp = connection->getAddress();
            QByteArray ind = connection->getIdentificator().toByteArray();

            if (!connection->getActive() || ind == "0")
                flag = true;

            if (ind == idIpPair[0] || ind == net::readNetManagerIdentificator()/*socketIp == ipStr || socketIp == local->ip().toString()*/)
            {
                flag = true;
                break;
            }
        }

        if (flag)
            continue;

        if (idIpPair.size() != 2)
        {
            qDebug() << "[Error][" << __LINE__ << "][" << __FILE__ << "]" << __FUNCTION__ << "] size!=2";
            continue;
        }

        addConnectionFromPair(QHostAddress(ipStr), serverPort);
    }
}

void NetManager::connectToServer(const quint16 &serverPort, QNetworkAddressEntry *local)
{
#ifdef ETALONIUM_CONSOLE
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
#ifdef ETALONIUM_CLIENT
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
                             const SocketPair &receiver)
{
    Config::Net::TypeSend send;
    if (Messages::isChainMessage(msgType) || Messages::isGeneralRequest(msgType) || msgType == 400
        || msgType == 402)
        send = Config::Net::TypeSend::ALL;
    else if (Messages::isGeneralResponse(msgType) || msgType == 401 || msgType == 403)
        send = Config::Net::TypeSend::FOCUSED;
    else
        send = Config::Net::TypeSend::EXCEPT;
    if (connections.isEmpty())
        saveToCache(message, msgType, receiver);

    for (const auto &tmp : connections)
    {
        bool isSend = false;

        switch (send)
        {
        case Config::Net::TypeSend::EXCEPT:
            isSend = tmp->getAddress().toStdString() != receiver.ip && tmp->getPort() != receiver.port;
            break;
        case Config::Net::TypeSend::FOCUSED:
            isSend = tmp->getAddress().toStdString() == receiver.ip && tmp->getPort() == receiver.port;
            break;
        case Config::Net::TypeSend::ALL:
            isSend = true;
            break;
        }

        if (!isSend)
            continue;
        if (tmp->getActive())
            tmp->distMsg(message, receiver);
        else
            saveToCache(message, msgType, receiver);
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
                             const SocketPair &receiver)
{
    QFile file("network_cache");
    file.open(QFile::Append);
    QByteArrayList list = { message, QByteArray::fromStdString(receiver.ip),
                            QByteArray::number(receiver.port), receiver.iden, QByteArray::number(msgType) };
    QByteArray package = Serialization::universalSerialize(list, 8);
    file.write(Utils::intToByteArray(package.length(), 8) + package);
    file.close();
}

void NetManager::sendFromCache()
{
    QFile file("network_cache");
    if (!file.exists())
        return;
    if (!file.open(QFile::ReadOnly))
        return;
    QByteArrayList allPackages = Serialization::universalDeserialize(file.readAll(), 8);
    file.close();
    file.remove();

    for (QByteArray packageData : allPackages)
    {
        QByteArrayList package = Serialization::universalDeserialize(packageData, 8);
        if (package.length() != 5)
            return;

        QByteArray data = package[0];
        SocketPair socketData;
        socketData.ip = package[1].toStdString();
        socketData.port = package[2].toShort();
        socketData.iden = package[3];
        unsigned int type = package[4].toUInt();
        sendMessage(data, type, socketData);
    }
}
void NetManager::distMessage(const QByteArray &data, const SocketPair &socketData)
{
#ifdef ETALONIUM_CLIENT
    bool flag = false;
    std::for_each(connections.begin(), connections.end(),
                  [&flag](SocketService *el) { flag = flag || el->getActive(); });

    if (flag)
    {
#endif
        for (int i = 0; i < connections.size(); i++)
            connections[i]->distMsg(data, socketData);
#ifdef ETALONIUM_CLIENT
    }
    else
    {
        QFile file("network_cache");
        file.open(QFile::Append);
        QByteArrayList list = { data, QByteArray::fromStdString(socketData.ip),
                                QByteArray::number(socketData.port), socketData.iden };
        QByteArray package = Serialization::universalSerialize(list, 8);
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
    qCritical() << "NET MANAGER: UPnP Error: " << msg;
}

void NetManager::upnpErrNet(QString msg)
{
    qCritical() << "NET MANAGER: UPnP Error: " << msg;
}

SocketService *NetManager::addConnectionFromPair(QHostAddress address, quint16 port)
{
    SocketService *socket = new SocketService(address.toString(), port);
    socket->setNetManager(this);
    connections.append(socket);
    connectSocket();
    qDebug() << "NET MANAGER: New connection is established : " << address << ":" << port;

    ThreadPool::addThread(connections.last());
    //    connections.last()->process();
    // QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    return connections.last();
}

void NetManager::addConnection(qint64 socketDescriptor)
{
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
        if (net::readNetManagerIdentificator() == i->getIdentificator().toByteArray())
            continue;

        connectionsList.append(
            Serialization::universalSerialize({ i->getID().toByteArray(), i->getAddress().toLocal8Bit() }));
    }
    return Serialization::universalSerialize(connectionsList);
}

void NetManager::checkOnValidConnection(QByteArray id, QByteArray address)
{
    QByteArray currId;
    QByteArray currAddress;
    QList<QByteArray> idAddressPair;
    for (auto i : tempConnections)
    {
        idAddressPair = Serialization::universalDeserialize(i);
        if (idAddressPair.size() != 2)
        {
            qDebug() << "[Error][" << __LINE__ << "][" << __FILE__ << "]" << __FUNCTION__ << "] size!=2";
            continue;
        }
        if (idAddressPair[1] == address && idAddressPair[0] != id)
        {
            removeConnectionByAddress(address);

            return;
        }
    }
}
