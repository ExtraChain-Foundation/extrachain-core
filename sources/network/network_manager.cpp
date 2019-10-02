#include "network/network_manager.h"

#include "network/packages/service/list_connections.h"

#include <QNetworkConfigurationManager>
#include <QRandomGenerator>
#include <QSettings>

using namespace Messages;

QList<SocketService *> NetManager::getConnections() const
{
    return connections;
}

NetManager::NetManager(AccountController *accountList, ActorIndex *actorIndex)
{
#ifdef ETALONIUM_CLIENT
    QSettings settings;

    if (!settings.value("network/serverIp").isValid())
        settings.setValue("network/serverIp", "51.68.181.52;51.68.181.53");
    if (!settings.value("network/allowLocalServer").isValid())
        settings.setValue("network/allowLocalServer", "false");

    serverIp = settings.value("network/serverIp").toString();
    allowLocalServer = settings.value("network/allowLocalServer").toBool();
#endif
    qDebug() << "Current server IPs:" << serverIp << "| allow local:" << allowLocalServer;

    //    deviceId = BigNumber(readNetManagerIdentificator());
    // ThreadPool::addThread(this);

    this->extPort = 2223;
    this->netPort = serverPort;
    qDebug() << "NET MANAGER: netport =" << netPort << "extPort =" << extPort;

    accounts = accountList;
    this->actorIndex = actorIndex;
    setupActorIndexConnections();
    findLocal();
    qDebug() << local->ip();
    qDebug() << "NET MANAGER: init net fun start";
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
}

void NetManager::process()
{
    startNetwork();
    connectToServer();
}

void NetManager::sendMessageTest()
{
    sendMessageTo(BigNumber("24"), "Yo-ma-yo");
}

void NetManager::showMessage(const QHostAddress &from, const QString &message)
{
    qDebug() << from.toIPv4Address() << " " << message;
}

void NetManager::resolverMessage(const QHostAddress &from, const QString &message)
{
    qDebug() << from.toIPv4Address() << " " << message;
}

NetManager::~NetManager()
{
    delete resolverService;
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
    const QHostAddress &localhost = QHostAddress(QHostAddress::LocalHost);
    QList<QHostAddress> localIpNotConnect;
    for (const QNetworkInterface &ni : QNetworkInterface::allInterfaces())
    {
        for (const QNetworkAddressEntry &address : ni.addressEntries())
        {
            if (address.ip().protocol() == QAbstractSocket::IPv4Protocol && address.ip() != localhost)
            {
                qDebug() << "NET MANAGER: local ip: " << address.ip().toString() << " " << ni;
                localIpNotConnect.append(address.ip());
            }
        }
    }

    QList<QNetworkInterface> nl = QNetworkInterface::allInterfaces();
    for (int i = 0; i < nl.size(); i++)
    {
        foreach (QNetworkAddressEntry entry, nl.at(i).addressEntries())
        {
            if (localIpNotConnect.contains(entry.ip()))
            {
                local = new QNetworkAddressEntry(entry);
                qDebug() << "Discovered local: " << local->ip().toString();
                if ((nl.at(i).type() == QNetworkInterface::Wifi)
                    || (nl.at(i).type() == QNetworkInterface::Ethernet))
                    i = nl.size();
            }
        }
    }
}

// void NetManager::restoreConnections()
//{
//    //
//    QHash<SocketPair, int>::iterator it;
//    for (it = disconnectedSocketList.begin(); it !=
//    disconnectedSocketList.end(); it++)
//    {
//        if (it.value() == maxValueTryConnections)
//        {
//            disconnectedSocketList.erase(it);
//        }
//        else
//        {
//            addConnectionFromPair(QHostAddress(QString::fromStdString(it.key().first)),
//                                  it.key().second);
//            checkConnection = it.key();
//            QTimer::singleShot(1000, this, SLOT(checkConnectionsStatus()));
//        }
//    }
//}

// void NetManager::checkConnectionsStatus()
//{

//    for (SocketService *el : connections)
//    {
//        if (!((el->getPort() == checkConnection.second)
//              && (el->getAddress().toStdString() == checkConnection.first)
//              && (el->getIdentificator() == checkConnection.getId())))
//            disconnectedSocketList[checkConnection]++;
//        else
//        {
//            QHash<SocketPair, int>::iterator it =
//            disconnectedSocketList.find(checkConnection);
//            disconnectedSocketList.erase(it);
//        }
//    }
//}
#include <iostream>
void NetManager::checkConnectionsStatus()
{
    bool flag = false;
    std::for_each(connections.begin(), connections.end(),
                  [&flag](SocketService *el) { flag = flag || el->getActive(); });
    emit qmlNetworkStatus(flag);
}
void NetManager::restoreConnections(const QList<SocketPair> &socketList)
{
    //
    for (const SocketPair &el : socketList)
    {
        addConnectionFromPair(QHostAddress(QString::fromStdString(el.first)), el.second);
    }
}

void NetManager::checkMyIdentificator()
{
    QObject *sender = QObject::sender();
    SocketService *connection = qobject_cast<SocketService *>(sender);
    if (allowLocalServer)
        if (net::readNetManagerIdentificator() == connection->getIdentificator())

            connection->removeMe();
    short counter = 0;
    std::for_each(connections.begin(), connections.end(), [connection, &counter](SocketService *el) {
        if (el->getIdentificator() == connection->getIdentificator())
        {
            if (el == connection)
                emit el->setActiveSignal(true);
            else
                emit el->removeMe();
        }
    });
    if (counter == 0)
        emit connection->setActiveSignal(true);
}

void NetManager::startNetwork()
{
    qDebug() << "NetManager::startNetwork()";
    netPort = serverPort;
    qDebug() << "NetPort:" << netPort;
    serverService = new ServerService(netPort, local);
    resolverService = new ResolverService(actorIndex);
    setupServerServiceConnections();
    serverService->startListen();
    setupResolverServiceConnections();
    ThreadPool::addThread(resolverService);
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

void NetManager::Verify(const QByteArray &block)
{
    const Block bl(block);
    if (actorIndex->validateBlock(bl))
        emit SendBlockExistence(bl);
    else
        qDebug() << "Error in local manager Verify, Block is not valid";
}

void NetManager::logDebug()
{
    qDebug() << "Networkmanager in other thread is work";
}

void NetManager::connectToServer()
{
#ifdef ETALONIUM_CONSOLE
    return;
#endif
    qDebug() << "void NetManager::connectToServer()";
    QStringList servers = serverIp.split(";");
    QString localIp = local->ip().toString();

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

void NetManager::setupActorIndexConnections()
{
    qDebug() << "NET MANAGER: setupActorIndexConnections";
    // from NetManager to ActorIndex
    connect(this, &NetManager::NewActor, actorIndex, &ActorIndex::addActor);
    connect(this, &NetManager::CheckActorExistence, actorIndex, &ActorIndex::handleNewActorCheck);

    // from ActorIndex to NetManager
    connect(actorIndex, &ActorIndex::ActorIsMissing, this, &NetManager::continueHandlingNewActor);
}

void NetManager::setupServerServiceConnections()
{
    connect(serverService, &ServerService::newConnection, this, &NetManager::addConnection);
#ifdef ETALONIUM_CLIENT
    connect(serverService, &ServerService::serverStatus, this, &NetManager::qmlServerError);
#endif
}

void NetManager::setupDiscoveryServiceConnections()
{
    //    connect(discoveryService, &DiscoveryService::ClientDiscovered, this,
    //            &NetManager::addConnectionFromPair);
}

void NetManager::setupResolverServiceConnections()
{
    qDebug() << "NET MANAGER: setupResolverServiceConnections";

    connect(resolverService, &ResolverService::secondWave, this, &NetManager::broadcastMsg);
    /**
        connect(resolverService, &ResolverService::reserveActor, this, &NetManager::sendReserveActorRequest);

        connect(resolverService, &ResolverService::getNewConnectionList, this,
       &NetManager::getNewConnectionList);

        connect(resolverService, &ResolverService::SendGetActor, this, &NetManager::sendGetActor);

        connect(resolverService, &ResolverService::getNewDfs, this, &NetManager::newDfsPack);

        connect(resolverService, &ResolverService::ReceiveProfile, this, &NetManager::receiveProfile);*/
    connect(this, &NetManager::receiveProfile, actorIndex, &ActorIndex::saveProfileFromNetwork);
    // server signals
    //    connect(client,             &Client::newMessage,
    //            resolverService,    &ResolverService::recieveMsg);

    //    connect(serverService,      &ServerService::MessageReceived,
    //            resolverService,    &ResolverService::recieveMsg);
    /**  connect(resolverService, &ResolverService::reserveActorResponse, this,
              &NetManager::handleReserveActorResponse);*/

    // spread signals/**
    /**
    connect(resolverService, &ResolverService::NewActor, this, &NetManager::handleNewActor);
    connect(resolverService, &ResolverService::NewBlock, this, &NetManager::handleNewBlock);
    connect(resolverService, &ResolverService::NewGenesisBlock, this, &NetManager::handleNewGenesisBlock);
    connect(resolverService, &ResolverService::NewTx, this, &NetManager::handleNewTx);
    connect(resolverService, &ResolverService::BlockApproved, this, &NetManager::handleBlockApproved);

    // request signals
    connect(resolverService, &ResolverService::GetActor, this, &NetManager::handleGetActor);
    connect(resolverService, &ResolverService::GetTx, this, &NetManager::handleGetTx);
    connect(resolverService, &ResolverService::CoinRequest, this, &NetManager::coinRequest);
    connect(resolverService, &ResolverService::GetTxPair, this, &NetManager::handleGetTxPair);
    connect(resolverService, &ResolverService::GetBlock, this, &NetManager::handleGetBlock);
    connect(resolverService, &ResolverService::GetBlockCount, this, &NetManager::handleGetBlockCount);
    connect(resolverService, &ResolverService::GetActorCount, this, &NetManager::handleGetActorCount);*/
    connect(this, &NetManager::requestBlockCount, this, &NetManager::sendGetBlockCount);
    connect(this, &NetManager::requestActorCount, this, &NetManager::sendGetActorCount);
    /**
        // responses
        connect(resolverService, &ResolverService::GetActorResponse, this,
       &NetManager::handleGetActorResponse); connect(resolverService, &ResolverService::GetActorCountResponse,
       this, &NetManager::handleGetActorCountResponse); connect(resolverService,
       &ResolverService::GetTxResponse, this, &NetManager::handleGetTxResponse); connect(resolverService,
       &ResolverService::GetTxPairResponse, this, &NetManager::handleGetTxPairResponse);
        connect(resolverService, &ResolverService::GetBlockResponse, this,
       &NetManager::handleGetBlockResponse); connect(resolverService, &ResolverService::GetBlockCountResponse,
       this, &NetManager::handleGetBlockCountResponse);*/

    // second waves signal

    //    connect(resolverService, &ResolverService::secondWavesMsg, this,
    //    &NetManager::sendDfsPack); connect(resolverService, &ResolverService::secondWavesRaw,
    //    this, &NetManager::sendDfsPack);

#ifdef ETALONIUM_CONSOLE
    connect(resolverService, &ResolverService::contractFromNetwork, this, &NetManager::shareContract);
#endif
    /**   connect(resolverService, &ResolverService::getDfsRequest, this, &NetManager::getDfsRequest);
       connect(resolverService, &ResolverService::downloadDfsResponse, this,
   &NetManager::downloadDfsResponse);
       //    connect(resolverService, &ResolverService::downloadDfsResponse, this,
       //            &NetManager::downloadDfsResponse);
       connect(resolverService, &ResolverService::broadcast, this, &NetManager::retranslateMessages);
       connect(resolverService, &ResolverService::downloadRequest, this, &NetManager::downloadDfsRequest);
       // list connections
       connect(resolverService, &ResolverService::createConnectionsList, this,
               &NetManager::createNewConnectionsFromList);*/
}

// Basic methods
void NetManager::broadcastMsg(const QByteArray &msg)
{
    SocketPair socketPair("0.0.0.0", 0, this);
    emit sendMsg(msg, socketPair);
}

void NetManager::sendMessage(const QByteArray &data, const QByteArray &messageType)
{
    BaseMessage msg(messageType);
    //    if (messageType != Messages::ACTOR_MESSAGE)
    //    signMessage(msg);
    QByteArray message = msg.init(data);
    broadcastMsg(message);
}

ResolverService *NetManager::getResolverService()
{
    return resolverService;
}
void NetManager::sendMsgToPeer(IMessage &msg, QHostAddress peerAddress)
{
    SocketPair socketPair(peerAddress.toString().toStdString(), 0, this);
    emit sendMsg(msg.serialize(), socketPair);
}

void NetManager::sendMsgToPeerPort(IMessage &msg, QHostAddress peerAddress, int port)
{
    SocketPair socketPair(peerAddress.toString().toStdString(), port, this);
    emit sendMsg(msg.serialize(), socketPair);
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
    qDebug() << "count of connections:: " << connections.size();
    SocketService *socket = new SocketService(address.toString(), port);
    connections.append(socket);
    qDebug() << 1;
    //    socket->setIdentificator(deviceId);
    qDebug() << 1;

    connect(this, &NetManager::sendMsg, connections.last(), &SocketService::sendMsg);

    qDebug() << "clientDisconnect with removeConnection connect:: status:   "
             << connect(connections.last(), &SocketService::clientDisconnected, this,
                        &NetManager::removeConnection);
    qDebug() << "NET MANAGER: New connection is established : " << address << ":" << port;

    connect(connections.last(), &SocketService::MessageReceived, resolverService,
            &ResolverService::recieveMsg);
    connect(connections.last(), &SocketService::removeMe, this, &NetManager::removeConnection);
    connect(connections.last(), &SocketService::checkMe, this, &NetManager::checkMyIdentificator);
#ifdef ETALONIUM_CLIENT
//    connectReconnect(connections.last());
#endif
    ThreadPool::addThread(connections.last());
    QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    return connections.last();
}

void NetManager::addConnection(qint64 socketDescriptor)
{
    qDebug() << "count of connections:: " << connections.size();
    SocketService *socket = new SocketService(socketDescriptor);
    qDebug() << 1;
    connections.append(socket);
    qDebug() << "clientDisconnect with removeConnection connect:: status:   "
             << connect(connections.last(), &SocketService::clientDisconnected, this,
                        &NetManager::removeConnection);
    connect(this, &NetManager::sendMsg, connections.last(), &SocketService::sendMsg);
    connect(connections.last(), &SocketService::MessageReceived, resolverService,
            &ResolverService::recieveMsg);
    connect(connections.last(), &SocketService::removeMe, this, &NetManager::removeConnection);
    connect(connections.last(), &SocketService::checkMe, this, &NetManager::checkMyIdentificator);
    QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));

    ThreadPool::addThread(connections.last());
}

void NetManager::removeConnection()
{
    QObject *sender = QObject::sender();
    SocketService *connection = qobject_cast<SocketService *>(sender);
    disconnect(connection, &SocketService::clientRemove, this, &NetManager::removeConnection);

    disconnect(this, &NetManager::sendMsg, connection, &SocketService::sendMsg);

    qDebug() << "clientDisconnect with removeConnection disconnect:: status:   "
             << disconnect(connection, &SocketService::clientDisconnected, this,
                           &NetManager::removeConnection);

    disconnect(connection, &SocketService::MessageReceived, resolverService, &ResolverService::recieveMsg);
    connections.removeAt(connections.indexOf(connection));
    connection->finished();

    checkConnectionsStatus();
}

void NetManager::sendProfile(PublicProfile profile)
{
    qDebug() << "send profile " << profile.profile.at(2);
    EntityMessage<PublicProfile> msg = Messages::createPublicProfileMessage(profile);

    if (accounts->getCurrentActor().getKey() == nullptr)
    {
        qDebug() << "Sorry not sorry";
        return;
    }

    signMessage(msg);
    broadcastMsg(msg.serialize());
}

//#ifdef ETALONIUM_CLIENT
// void NetManager::addEntryPoint(QTcpSocket *newEntryPoint)
//{
//    for (SocketService *connection : entryPoints)
//    {
//        if (connection->getAddress() ==
//        newEntryPoint->peerAddress().toString()
//            || newEntryPoint->peerAddress().toIPv4Address() ==
//            local->ip().toIPv4Address())
//        {
//            qDebug() << "NET MANAGER: Can't add connection (already
//            established): "
//                     << newEntryPoint->peerAddress();
//            return;
//        }
//    }
//    entryPoints.append(new SocketService(newEntryPoint));
//    //    connections.append(entryPoints.last());
//    //    ThreadPool::addThread(entryPoints.last());
//    connect(entryPoints.last(), &SocketService::clientDisconnected, this,
//            &NetManager::remSocket);

//    qDebug() << "NET MANAGER: New entry point is established : "
//             << newEntryPoint->peerAddress().toString() << ":" <<
//             newEntryPoint->peerPort();
//    connect(entryPoints.last(), &SocketService::MessageReceived,
//    resolverService,
//            &ResolverService::recieveMsg);

//    //    connect(connections.last(), &SocketService::finished,
//    //            this,               &NetManager::removeConnection);
//    //    sendGetActorCount();

//    //    connectionsList.append(newConnection->peerAddress());
//    //    ConnectionList connectionList;
//    //    for (auto current : connections) {
//    //        connectionList.addConnection(current->getSocketAddress());
//    //    }
//    //    sendConnectionList(connectionList);
//}
//#endif

void NetManager::reserveActor(const bool account)
{
    //    int r = rand();
    qsrand(QDateTime().currentMSecsSinceEpoch());
    int temp = qrand() % ((30000 + 1) - 10000) + 10000;
    //    QByteArray werHash = hash.toUtf8() + QByteArray::number(temp);
    BigNumber logHash("a124");
    EntityMessage<BigNumber> msg = Messages::createReserveActorMessage(logHash);
    getReserveActorHandlers.insert(calcHash(msg), GetEntityHandler<BigNumber>());
    broadcastMsg(msg.serialize());
    //    qDebug() << "ololo";
}

void NetManager::retranslateMessages(const QByteArray &msg, QString peerAddress)
{
    SocketPair socketPair(peerAddress.toStdString(), 0, this);
    emit sendMsg(msg, socketPair);
}

void NetManager::signMessage(Messages::IMessage &message) const
{
    //    qDebug() << "NET MANAGER: signMessage" <<
    //    accounts->getCurrentActor().serialize();
    message.calcDigSig(accounts->getCurrentActor());
}

QByteArray NetManager::calcHash(Messages::IMessage &message) const
{
    return Utils::calcKeccak(message.serialize());
}

void NetManager::getNewConnectionList(QList<QByteArray> newConList)
{
    for (auto addCon : newConList)
    {
        addConnectionFromPair(QHostAddress(QString(addCon)), 1616);
    }
}

void NetManager::createNewConnectionsFromList(const QByteArray &message)
{
    EnableConnections msg(message);
    QList<std::pair<int, std::string>> list = msg.getEnableConnections();
    for (auto &el : list)
    {
        SocketService *newSock = new SocketService(QString::fromStdString(el.second), el.first);
        if (connections.indexOf(newSock) == -1)
        {
            connections.append(newSock);
            ThreadPool::addThread(connections.last());
            connect(connections.last(), &SocketService::clientRemove, this, &NetManager::removeConnection);

            connect(this, &NetManager::sendMsg, connections.last(), &SocketService::sendMsg);
            qDebug() << "NET MANAGER: New connection is established : " << newSock->getAddress() << ":"
                     << newSock->getPort();
            connect(connections.last(), &SocketService::MessageReceived, resolverService,
                    &ResolverService::recieveMsg);
            connect(connections.last(), &SocketService::removeMe, this, &NetManager::removeConnection);
            connect(connections.last(), &SocketService::checkMe, this, &NetManager::checkMyIdentificator);
        }
    }
}

// Send messages //
void NetManager::sendReserveActorRequest(QString peerAddress, QByteArray requestHash, const int port)
{
    for (auto i : reservedActorList)
        qDebug() << i;

    reservedActorListUse = true;
    sendCompanyActor(peerAddress);
    PublicProfile profile = actorIndex->getProfileToSend("0");
    if (profile.sign != "")
    {
        EntityMessage<PublicProfile> msg2 = Messages::createPublicProfileMessage(profile);
        broadcastMsg(msg2.serialize());
    }
    BigNumber reserveActorId = actorIndex->getLastSavedId() + 1;
    while (reservedActorList.contains(reserveActorId))
    {
        ++reserveActorId;
    }
    reservedActorList.append(reserveActorId);
    EntityResponseMessage<BigNumber> msg = Messages::createReserveActorResponse(reserveActorId, requestHash);
    signMessage(msg);
    broadcastMsg(msg.serialize());
    qDebug() << msg.serialize();
    reservedActorListUse = false;
}

void NetManager::sendConnectionList(EnableConnections sendConList, SocketService *addressant)
{
    signMessage(sendConList);
    SocketPair socketPair(addressant->getAddress().toStdString(), addressant->getPort(), this);
    emit sendMsg(sendConList.serialize(), socketPair);
}

void NetManager::sendCoinRequest(BigNumber amount)
{
    EntityMessage<BigNumber> msg = Messages::createRequestCoinMessage(amount);
    signMessage(msg);
    broadcastMsg(msg.serialize());
    qDebug() << "NetManager::sendCoinRequest: amount - " << amount;
}

void NetManager::sendDfsPack(const Messages::DfsMessage &msg)
{
    broadcastMsg(msg.serialize());
    //#ifdef ETALONIUM_CLIENT
    //    for (SocketService *connect : entryPoints)
    //    {
    //        connect->sendMsg(msg.serialize());
    //    }
    //#endif
}

void NetManager::sendDfsMessageTo(DfsMessage dfs, QString peerAddress)
{
    //    signMessage(msg);
    sendMsgToPeer(dfs, QHostAddress(peerAddress));
}

void NetManager::sendDfsRequest(const DfsRequest &msg)
{
    broadcastMsg(msg.serialize());
    //#ifdef ETALONIUM_CLIENT
    //    for (SocketService *connect : entryPoints)
    //    {
    //        connect->sendMsg(msg.serialize());
    //    }
    //#endif
}

void NetManager::downloadAnswer(bool status, QByteArray header, QString peerAddressst)
{
    DownloadDfsRequestData package(status, header);
    EntityMessage<DownloadDfsRequestData> msg = Messages::createDownloadDfsRequest(package);
    signMessage(msg);
    sendMsgToPeer(msg, QHostAddress(peerAddressst));
}

void NetManager::sendNewActor(Actor<KeyPublic> actor)
{
    qDebug() << "NET MANAGER: Send new actor";
    //    reservedActorList.removeAt(reservedActorList.indexOf(actor.getId()));
    EntityMessage<Actor<KeyPublic>> msg = Messages::createActorMessage(actor);
    GetEntityHandler<BigNumber> handler;
    handler.addResponse(actor.getId());
    getReserveActorHandlers.insert(calcHash(msg), handler);
    //    signMessage(msg);
    broadcastMsg(msg.serialize());
}

void NetManager::sendNewTx(Transaction tx)
{
    EntityMessage<Transaction> msg = Messages::createTxMessage(tx);

    signMessage(msg);
    broadcastMsg(msg.serialize());
}

void NetManager::sendNewContract(Contract contract)
{
    qDebug() << "NetManager::sendNewContract: " << contract.serialize();
    EntityMessage<Contract> msg = Messages::createContractMessage(contract);
    signMessage(msg);
    broadcastMsg(msg.serialize());
}

void NetManager::sendNewBlock(Block block)
{
    EntityMessage<Block> msg = Messages::createBlockMessage(block);
    signMessage(msg);
    broadcastMsg(msg.serialize());
}

void NetManager::sendTxResponse(Transaction tx, SearchEnum::TxParam param, QString value,
                                QHostAddress peerAddress, QByteArray requestHash)
{
    qDebug() << "NET MANAGER: Sending tx" << tx.getHash() << "to" << peerAddress.toString();
    EntityResponseMessage<Transaction> msg = Messages::createGetTxResponse(tx, requestHash);
    signMessage(msg);
    //    sendMsgToPeer(msg, peerAddress); fix it
}

void NetManager::sendTxPairResponse(TxPair pair, QHostAddress peerAddress, QByteArray requestHash)
{
    qDebug() << "NET MANAGER: Sending txPair" << pair.serialize() << "to" << peerAddress.toString();
    EntityResponseMessage<TxPair> msg = Messages::createGetTxPairResponse(pair, requestHash);
    signMessage(msg);
    //    sendMsgToPeer(msg, peerAddress); fix it
}

void NetManager::sendBlockResponse(Block block, SearchEnum::BlockParam param, QString value,
                                   QHostAddress peerAddress, QByteArray requestHash)
{
    qDebug() << "NET MANAGER: Sending block" << block.serialize() << "to" << peerAddress.toString();
    EntityResponseMessage<Block> msg = Messages::createGetBlockResponse(block, requestHash);
    signMessage(msg);
    sendMsgToPeer(msg, peerAddress);
}

void NetManager::sendBlockCountResponse(BigNumber blockCount, QHostAddress peerAddress,
                                        QByteArray requestHash)
{
    qDebug() << "NET MANAGER: Sending block count" << blockCount << "to" << peerAddress.toString();
    EntityResponseMessage<BigNumber> msg = createGetBlockCountResponse(blockCount, requestHash);
    //    signMessage(msg);
    sendMsgToPeer(msg, peerAddress);
}

void NetManager::sendActorCountResponse(BigNumber actorCount, QHostAddress peerAddress,
                                        QByteArray requestHash)
{
    qDebug() << "NET MANAGER: Sending actor count" << actorCount << "to" << peerAddress.toString();
    EntityResponseMessage<BigNumber> msg = createGetActorCountResponse(actorCount, requestHash);
    //    signMessage(msg);
    sendMsgToPeer(msg, peerAddress);
}

void NetManager::sendGenesisBlock(Block prevBlock, QByteArray prevGenHash)
{
    qDebug() << "NET MANAGER: Sending genesis block";
    GenesisBlock *genBlock = Blockchain::readGenesisBlock(prevBlock, prevGenHash);
    if (genBlock == nullptr)
    {
        qCritical() << "NET MANAGER: Error while sending genesis block";
        return;
    }

    // sign block
    genBlock->sign(accounts->getCurrentActor());

    EntityMessage<Block> msg = Messages::createGenesisBlockMessage(*genBlock);

    delete genBlock;
    QFile::remove(DataStorage::TMP_GENESIS_BLOCK);

    signMessage(msg);
    broadcastMsg(msg.serialize());
}

// Send messages //

void NetManager::sendGetActor(BigNumber actorId)
{
    qDebug() << "NET MANAGER: Requesting actor with id =" << actorId;
    GetActorMessage msg(actorId);
    //    signMessage(msg);
    getActorsHandlers.insert(calcHash(msg), GetEntityHandler<Actor<KeyPublic>>());
    broadcastMsg(msg.serialize());
}

void NetManager::shareContract(Contract contract)
{
    //    if (contract.makeFirstTransction()) {
    //        emit contractFirstTransaction(contract);
    //        return;
    //    }
    qDebug() << contract.serialize();
    if (contract.makeFinalTransaction())
    {
        emit contractFinalTransaction(contract);
        return;
    }
    EntityMessage<Contract> msg = Messages::createContractMessage(contract);
    signMessage(msg);
    broadcastMsg(msg.serialize());
}

void NetManager::sendMessageTo(BigNumber recipientId, QByteArray message)
{
    qDebug() << "NET MANAGER: send message to " << recipientId;
    ChatMessage msg(recipientId, message);
    qDebug() << msg.serialize();
    signMessage(msg);
    broadcastMsg(msg.serialize());
}

void NetManager::sendGetBlock(BlockParam param, QString value)
{
    qDebug() << "NET MANAGER: Requesting block by" << toString(param) << "and" << value;
    GetBlockMessage msg(param, value.toLocal8Bit());
    //    signMessage(msg);
    getBlockHandlers.insert(calcHash(msg), GetEntityHandler<Block>());
    qDebug() << "<<<<<<<<<<<<<< " << calcHash((msg));
    broadcastMsg(msg.serialize());
}

void NetManager::sendGetBlockCount()
{
    qDebug() << "NET MANAGER: Requesting block count";
    BaseMessage msg = Messages::createGetBlockCountMessage();
    //    signMessage(msg);
    getCountHandlers.insert(calcHash(msg), GetCountHandler());
    broadcastMsg(msg.serialize());
}

void NetManager::sendGetActorCount()
{
    qDebug() << "NET MANAGER: Requesting actor count";
    BaseMessage msg = Messages::createGetActorCountMessage();
    //    signMessage(msg);
    getCountHandlers.insert(calcHash(msg), GetCountHandler());
    broadcastMsg(msg.serialize());

    //    emit creaTx();
}

void NetManager::sendGetTx(TxParam param, QString value)
{
    qDebug() << "NET MANAGER: Requesting tx by" << toString(param) << "and" << value;
    GetTxMessage msg(param, value.toLocal8Bit());
    signMessage(msg);
    getTxHandlers.insert(calcHash(msg), GetEntityHandler<Transaction>());
    broadcastMsg(msg.serialize());
}

void NetManager::sendGetTxPair(BigNumber sender, BigNumber receiver)
{
    qDebug() << "NET MANAGER: Requesting tx pair. Sender:" << sender << ", Receiver:" << receiver;
    GetTxPairMessage msg(sender, receiver);
    signMessage(msg);
    getTxPairHandlers.insert(calcHash(msg), GetEntityHandler<TxPair>());
    broadcastMsg(msg.serialize());
}

void NetManager::sendCompanyActor(QString peerAddress)
{
    EntityMessage<Actor<KeyPublic>> msg = Messages::createActorMessage(actorIndex->getActor(BigNumber(0)));
    sendMsgToPeer(msg, QHostAddress(peerAddress));
}

// Handling messsages ///

void NetManager::handleNewActor(Actor<KeyPublic> actor, const QByteArray &requestHash,
                                QHostAddress peerAddress)
{
    qDebug() << "NET MANAGER: Handling NewActor" << actor.toString() << "from" << peerAddress.toString();
    if (reservedActorList.indexOf(actor.getId()) == -1)
        emit NewActor(actor);
    else
    {
        BigNumber freeActorId = actor.getId() + BigNumber("1");
        while (reservedActorList.contains(freeActorId))
        {
            freeActorId++;
        }

        EntityResponseMessage<BigNumber> msg = Messages::createReserveActorResponse(freeActorId, requestHash);
        signMessage(msg);
        reservedActorList.append(freeActorId);
        sendMsgToPeer(msg, peerAddress);
    }
}
void NetManager::continueHandlingNewActor(Actor<KeyPublic> actor)
{
    EntityMessage<Actor<KeyPublic>> msg = Messages::createActorMessage(actor);
    signMessage(msg);
    broadcastMsg(msg.serialize());
}

//===================================DFSpackage===================================
// void NetManager::sendDfsPackage()
//{
//    EntityMessage<Actor<KeyPublic>> msg = Messages::createActorMessage(actor);
//    signMessage(msg);
//    broadcastMsg(msg);
//}
//================================================================================

void NetManager::handleNewBlock(Block block, QHostAddress peerAddress)
{
    qDebug() << "NET MANAGER: Handling NewBlock" << block.toString() << "from" << peerAddress.toString();
    emit CheckBlockExistence(block);
}

void NetManager::continueHandlingNewBlock(Block block)
{
    sendNewBlock(block);
}

void NetManager::handleNewGenesisBlock(Block block, QHostAddress peerAddress)
{
    qDebug() << "NET MANAGER: Handling NewGenesisBlock" << block.toString() << "from"
             << peerAddress.toString();

    emit AddBlock(block);

    EntityMessage<Block> msg = Messages::createGenesisBlockMessage(block);
    signMessage(msg);
    broadcastMsg(msg.serialize());
}

void NetManager::handleNewTx(Transaction tx, QHostAddress peerAddress)
{
    qDebug() << "NET MANAGER: Handling newTx" << tx.toString() << "from" << peerAddress.toString();

    // If there are hops -> spread message forward
    if (tx.getHop() > 0)
    {
        tx.decrementHop();

        EntityMessage<Transaction> msg = Messages::createTxMessage(tx);
        //        signMessage(msg);
        broadcastMsg(msg.serialize());
        return;
    }

    emit NewTx(tx);
}

void NetManager::handleBlockApproved(BigNumber blockId, BigNumber approver, QHostAddress peerAddress)
{
    qDebug() << "NET MANAGER: Handling BlockApproved" << blockId << "from" << peerAddress.toString();
    emit BlockApproved(blockId, approver, peerAddress);
}

void NetManager::handleGetActor(BigNumber actorId, QHostAddress peerAddress, QByteArray requestHash)
{
    qDebug() << "NET MANAGER: Handling request: getActor" << actorId << "from" << peerAddress.toString();
    //    if(actorIndex->actorExist(actorId)) {
    Actor<KeyPublic> actor = actorIndex->getActor(actorId);
    if (actor.isEmpty())
    {
        qDebug() << "NET MANAGER: Can't handle request: There no actor with id" << actorId << "locally";
        return;
    }
    EntityResponseMessage<Actor<KeyPublic>> msg = Messages::createGetActorResponse(actor, requestHash);

    //    signMessage(msg);
    sendMsgToPeer(msg, peerAddress);
    PublicProfile profile = actorIndex->getProfileToSend(actor.getId().toString());
    if (profile.sign != "")
    {
        EntityMessage<PublicProfile> msg2 = Messages::createPublicProfileMessage(profile);
        qDebug() << "send profile " << actor.getId();
        sendMsgToPeer(msg2, peerAddress);
    }
    //    } else {
    //        //send massage actor is not exist or not
    //    }
}

void NetManager::handleGetTx(TxParam param, QByteArray value, QHostAddress peerAddress,
                             QByteArray requestHash)
{
    qDebug() << "NET MANAGER: Handling request: getTx" << toString(param) << value << "from"
             << peerAddress.toString();
    emit GetTx(param, value, peerAddress, requestHash);
}

void NetManager::handleGetTxPair(BigNumber sender, BigNumber receiver, QHostAddress peerAddress,
                                 QByteArray requestHash)
{
    qDebug() << "NET MANAGER: Handling request: getTxPair sender=" << sender << "receiver=" << receiver
             << "from" << peerAddress.toString();
    emit GetTxPair(sender, receiver, peerAddress, requestHash);
}

void NetManager::handleGetBlock(BlockParam param, QByteArray value, QHostAddress peerAddress,
                                QByteArray requestHash)
{
    qDebug() << "NET MANAGER: Handling request: getBlock" << toString(param) << value << "from"
             << peerAddress.toString();
    emit GetBlock(param, value, peerAddress, requestHash);
}

void NetManager::handleGetBlockCount(const QHostAddress &peerAddress, const QByteArray &requestHash)
{
    qDebug() << "NET MANAGER: Handling request: getBlockCount from" << peerAddress.toString();
    emit GetBlockCount(peerAddress, requestHash);
}

void NetManager::handleGetActorCount(const QHostAddress &peerAddress, const QByteArray &requestHash)
{
    qDebug() << "NET MANAGER: Handling request: getActorCount from" << peerAddress.toString();
    emit GetActorCount(peerAddress, requestHash);
}

void NetManager::handleGetActorResponse(Actor<KeyPublic> actor, QByteArray reqHash, QHostAddress peerAddress)
{
    qDebug() << "NET MANAGER: handleGetActorResponse(): " << actor.getId();
    // if handler doesn't exists
    if (!getActorsHandlers.contains(reqHash))
    {
        qDebug() << "NET MANAGER: Error: not waiting for getActor responses with "
                    "reqHash="
                 << reqHash;
        return;
    }

    qDebug() << "NET MANAGER: Handling response: actor" << actor.getId() << "from" << peerAddress.toString();
    GetEntityHandler<Actor<KeyPublic>> handler = getActorsHandlers[reqHash];
    handler.addResponse(actor);
    getActorsHandlers.insert(reqHash, handler);

    if (handler.canProcess())
    {
        Actor<KeyPublic> toAdd = handler.resolveBestEntity();
        if (!toAdd.isEmpty())
        {
            qDebug() << "NET MANAGER: Adding new Actor" << toAdd.toString();
            emit NewActor(actor);
            // clear handler
            getTxHandlers.remove(reqHash);
            return;
        }
        else
        {
            // if we have controversial situation - wait for some more requests
            qDebug() << "NET MANAGER: Can't resolve best Actor entity";
        }
    }

    qDebug() << "NET MANAGER: Waiting for more GetActor [" << reqHash << "] responses";
}

void NetManager::handleGetTxResponse(Transaction tx, QByteArray reqHash, QHostAddress peerAddress)
{
    // if handler doesn't exists
    if (!getTxHandlers.contains(reqHash))
    {
        qDebug() << "NET MANAGER: Error: not waiting for getTx responses with reqHash=" << reqHash;
        return;
    }

    qDebug() << "NET MANAGER: Handling response: tranaction" << tx.getHash() << "from"
             << peerAddress.toString();
    GetEntityHandler<Transaction> handler = getTxHandlers[reqHash];
    handler.addResponse(tx);
    getTxHandlers.insert(reqHash, handler);

    if (handler.canProcess())
    {
        Transaction toAdd = handler.resolveBestEntity();
        if (!toAdd.isEmpty())
        {
            // validate tx
            if (actorIndex->validateTx(toAdd))
            {
                qDebug() << "NET MANAGER: Adding new Tx" << toAdd.toString();
                emit TxResponse(tx, peerAddress);
                // clear handler
                getTxHandlers.remove(reqHash);

                return;
            }
            else
            {
                qDebug() << "NET MANAGER: Warning: Received tx" << toAdd.toString() << "is not valid.";
            }
        }
        else
        {
            // if we have controversial situation - wait for some more requests
            qDebug() << "NET MANAGER: Can't resolve best Transaction entity";
        }
    }

    qDebug() << "NET MANAGER: Waiting for more GetTx [" << reqHash << "] responses";
}

void NetManager::handleGetTxPairResponse(TxPair pair, QByteArray reqHash, QHostAddress peerAddress)
{
    // if handler doesn't exists
    if (!getTxHandlers.contains(reqHash))
    {
        qDebug() << "NET MANAGER: Error: not waiting for getTx responses with reqHash=" << reqHash;
        return;
    }

    qDebug() << "NET MANAGER: Handling response: txPair" << pair.serialize() << "from"
             << peerAddress.toString();
    GetEntityHandler<TxPair> handler = getTxPairHandlers[reqHash];

    handler.addResponse(pair);
    getTxPairHandlers.insert(reqHash, handler);

    if (handler.canProcess())
    {
        TxPair toAdd = handler.resolveBestEntity();
        if (!toAdd.isEmpty())
        {
            // validate tx pair
            if (actorIndex->validateTx(pair.getFirst()) && actorIndex->validateTx(pair.getSecond()))
            {
                qDebug() << "NET MANAGER: Adding new TxPair" << toAdd.serialize();
                emit TxPairResponse(pair, peerAddress);
                // clear handler
                getTxHandlers.remove(reqHash);

                return;
            }
            else
            {
                qWarning() << "NET MANAGER: Warning: Received TxPair" << toAdd.serialize() << "is not valid.";
            }
        }
        else
        {
            // if we have controversial situation - wait for some more requests
            qWarning() << "NET MANAGER: Can't resolve best TxPair entity";
        }
    }

    qDebug() << "NET MANAGER: Waiting for more GetTxPair [" << reqHash << "] responses";
}

void NetManager::handleGetBlockResponse(Block block, QByteArray reqHash, QHostAddress peerAddress)
{
    // if handler doesn't exists
    qDebug() << ">>>>>>>>>>" << reqHash;
    if (!getBlockHandlers.contains(reqHash))
    {
        qDebug() << "NET MANAGER: Error: not waiting for getBlock responses with "
                    "reqHash="
                 << reqHash;
        return;
    }

    qDebug() << "NET MANAGER: Handling response: block" << block.getIndex() << "from"
             << peerAddress.toString();
    GetEntityHandler<Block> handler = getBlockHandlers[reqHash];
    handler.addResponse(block);
    getBlockHandlers.insert(reqHash, handler);

    if (handler.canProcess())
    {
        Block toAdd = handler.resolveBestEntity();
        if (!toAdd.isEmpty())
        {
            // validate block
            if (actorIndex->validateBlock(toAdd))
            {
                qDebug() << "NET MANAGER: Adding new Block" << toAdd.toString();
                emit AddBlock(toAdd);
                // clear handler
                getBlockHandlers.remove(reqHash);

                return;
            }
            else
            {
                qDebug() << "NET MANAGER: Warning: Received block" << toAdd.toString() << "is not valid.";
            }
        }
        else
        {
            // if we have controversial situation - wait for some more requests
            qDebug() << "NET MANAGER: Can't resolve best block entity";
        }
    }

    qDebug() << "NET MANAGER: Waiting for more GetBlock [" << reqHash << "] responses";
}

void NetManager::handleGetBlockCountResponse(BigNumber blockCount, QByteArray reqHash,
                                             QHostAddress peerAddress)
{
    // if handler doesn't exists
    if (!getCountHandlers.contains(reqHash))
    {
        qDebug() << "NET MANAGER: Error: not waiting for block count responses "
                    "with reqHash="
                 << reqHash;
        return;
    }

    qDebug() << "NET MANAGER: Handling response: block count" << blockCount << "from"
             << peerAddress.toString();
    GetCountHandler handler = getCountHandlers[reqHash];
    handler.addResponse(blockCount);
    getCountHandlers.insert(reqHash, handler);

    if (handler.canProcess())
    {
        BigNumber searchedValue = handler.getSearchedValue();
        this->maxBlockCount = searchedValue;
        qDebug() << "NET MANAGER: Max block count is set to" << searchedValue;
        emit BlockCountResponse(searchedValue, peerAddress);
        //        block

        // clear handler
        getCountHandlers.remove(reqHash);
    }

    qDebug() << "NET MANAGER: Waiting for more GetBlockCount [" << reqHash << "] responses";
}

void NetManager::handleGetActorCountResponse(BigNumber actorCount, QByteArray reqHash,
                                             QHostAddress peerAddress)
{
    // if handler doesn't exists
    if (!getCountHandlers.contains(reqHash))
    {
        qDebug() << "NET MANAGER: Error: not waiting for account count responses "
                    "with reqHash="
                 << reqHash;
        return;
    }
    qDebug() << "NET MANAGER: handleGetActorCountResponse: count" << actorCount;
    qDebug() << actorIndex->getLastSavedId();
    BigNumber currentActorCount = actorIndex->getLastSavedId();
    if (currentActorCount > actorCount)
    {
        while (currentActorCount > actorCount)
        {
            actorCount = actorCount + 1;
            sendNewActor(actorIndex->getActor(actorCount));
            qDebug() << "NET MANAGER: handleGetActorCountResponse: " << currentActorCount;
        }
        return;
    }
    sendGetActor(BigNumber(0));
    while (currentActorCount < actorCount)
    {
        currentActorCount = currentActorCount + 1;
        sendGetActor(currentActorCount);
        qDebug() << "NET MANAGER: handleGetActorCountResponse: " << currentActorCount;
    }
    //    sendGetBlockCount();
}

void NetManager::handleReserveActorResponse(const BigNumber &actorId, const QByteArray &requestHash,
                                            const QString &peerAdress)
{
    if (!getReserveActorHandlers.contains(requestHash))
        return;

    GetEntityHandler<BigNumber> handler = getReserveActorHandlers[requestHash];
    //    if (handler.)
    handler.addResponse(actorId);
    getReserveActorHandlers.insert(requestHash, handler);
    if (handler.canProcess())
    {
        if (!actorIndex->getActor(0).isEmpty())
        {
            qDebug() << "1234567890987654321";
        }

        accounts->createActorWithId(actorId, true);
    }
}
