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
    requestResponseMap = new QMap<QByteArray, int>();
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
    //    setupActorIndexConnections();
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
    //    resolverService = new ResolverService(actorIndex, requestResponseMap);
    setupServerServiceConnections();
    serverService->startListen();
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

// void NetManager::setupResolverServiceConnections()
//{
//    qDebug() << "NET MANAGER: setupResolverServiceConnections";

//    connect(resolverService, &ResolverService::secondWave, this, &NetManager::broadcastMsg);

//    //    connect(resolverService, &ResolverService::SendGetActor, this, &NetManager::sendGetActor);

//    connect(resolverService, &ResolverService::newDfsPack, this, &NetManager::newDfsPack);

//    connect(resolverService, &ResolverService::receiveProfile, this, &NetManager::receiveProfile);
//    connect(this, &NetManager::receiveProfile, actorIndex, &ActorIndex::saveProfileFromNetwork);

//    // spread signals

//    connect(resolverService, &ResolverService::newActor, actorIndex, &ActorIndex::handleNewActor);
//    connect(resolverService, &ResolverService::getActor, actorIndex, &ActorIndex::getActor);
//    connect(resolverService, &ResolverService::getActorsCount, this, &NetManager::GetActorCount);

//    connect(resolverService, &ResolverService::newBlock, this, &NetManager::AddBlock);
//    connect(resolverService, &ResolverService::getBlock, this, &NetManager::GetBlock);
//    connect(resolverService, &ResolverService::getBlocksCount, this, &NetManager::GetBlockCount);
//    //    connect(resolverService, &ResolverService::NewGenesisBlock, this,
//    //    &NetManager::handleNewGenesisBlock);
//    connect(resolverService, &ResolverService::newTx, this, &NetManager::NewTx);
//    connect(resolverService, &ResolverService::getTx, this, &NetManager::GetTx);
//    connect(resolverService, &ResolverService::getTxPair, this, &NetManager::GetTxPair);

//    //    connect(resolverService, &ResolverService::BlockApproved, this, &NetManager::handleBlockApproved);

//    // request signals
//    connect(resolverService, &ResolverService::getActor, actorIndex, &ActorIndex::handleGetActor);
//    /**
//    connect(resolverService, &ResolverService::GetActor, this, &NetManager::handleGetActor);
//    connect(resolverService, &ResolverService::GetTx, this, &NetManager::handleGetTx);
//    connect(resolverService, &ResolverService::CoinRequest, this, &NetManager::coinRequest);
//    connect(resolverService, &ResolverService::GetTxPair, this, &NetManager::handleGetTxPair);
//    connect(resolverService, &ResolverService::GetBlock, this, &NetManager::handleGetBlock);
//    connect(resolverService, &ResolverService::GetBlockCount, this, &NetManager::handleGetBlockCount);
//    connect(resolverService, &ResolverService::GetActorCount, this, &NetManager::handleGetActorCount);
//    */
//    connect(this, &NetManager::requestBlockCount, this, &NetManager::sendGetBlockCount);
//    connect(this, &NetManager::requestActorCount, this, &NetManager::sendGetActorCount);
//    /*********************************************************************************************/

//    // responses
//    /**
//    connect(resolverService, &ResolverService::GetActorResponse, this, &NetManager::handleGetActorResponse);
//    connect(resolverService, &ResolverService::GetActorCountResponse, this,
//            &NetManager::handleGetActorCountResponse);
//    connect(resolverService, &ResolverService::GetTxResponse, this, &NetManager::handleGetTxResponse);
//    connect(resolverService, &ResolverService::GetTxPairResponse, this,
//    &NetManager::handleGetTxPairResponse); connect(resolverService, &ResolverService::GetBlockResponse,
//    this, &NetManager::handleGetBlockResponse); connect(resolverService,
//    &ResolverService::GetBlockCountResponse, this,
//            &NetManager::handleGetBlockCountResponse);
//    */

//#ifdef ETALONIUM_CONSOLE
//    // connect(resolverService, &ResolverService::contractFromNetwork, this, &NetManager::shareContract);
//#endif
//}

// Basic methods
void NetManager::broadcastMsg(const QByteArray &msg)
{
    SocketPair socketPair("0.0.0.0", 0, this);
    emit sendMsg(msg, socketPair);
}

void NetManager::sendMessage(const QByteArray &data, const QByteArray &msgType)
{
    BaseMessage msg(msgType);
    if (msgType != Messages::ACTOR_MESSAGE)
        signMessage(msg);

    QByteArray message = msg.init(data);
    //    if (!addResponseHandler(message, messageType))
    //    {
    //        FileList list;
    //        QFile file(".handler");
    //        list.setFileList(file);
    //        list.add(msg.hash(), "0");
    //    }
    broadcastMsg(message);
}

void NetManager::sendMessageResponse(const QByteArray &data, const QByteArray &msgType,
                                     const QByteArray &requestHash, const SocketPair &receiver)
{
    BaseMessageResponse rmsg(data, requestHash, msgType);
    signMessage(rmsg);

    emit sendMsg(rmsg.serialize(), receiver);
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

// ResolverService *NetManager::getResolverService()
//{
//    return resolverService;
//}

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
    connections.append(socket);
    connect(this, &NetManager::sendMsg, connections.last(), &SocketService::sendMsg);
    qDebug() << "[&netManager connection status] clientDisconnect with removeConnection connect:: status:"
             << connect(connections.last(), &SocketService::clientDisconnected, this,
                        &NetManager::removeConnection);
    qDebug() << "NET MANAGER: New connection is established : " << address << ":" << port;

    connect(connections.last(), &SocketService::MessageReceived, this, &NetManager::MessageReceived);
    connect(connections.last(), &SocketService::removeMe, this, &NetManager::removeConnection);
    connect(connections.last(), &SocketService::checkMe, this, &NetManager::checkMyIdentificator);
    ThreadPool::addThread(connections.last());
    QTimer::singleShot(3000, this, SLOT(checkConnectionsStatus()));
    return connections.last();
}

void NetManager::addConnection(qint64 socketDescriptor)
{
    SocketService *socket = new SocketService(socketDescriptor);
    connections.append(socket);
    qDebug() << "[&netManager connection status] clientDisconnect with removeConnection connect:: status:"
             << connect(connections.last(), &SocketService::clientDisconnected, this,
                        &NetManager::removeConnection);
    connect(this, &NetManager::sendMsg, connections.last(), &SocketService::sendMsg);
    connect(connections.last(), &SocketService::MessageReceived, this, &NetManager::MessageReceived);
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
    qDebug() << "[&netManager disconnect status] clientDisconnect with removeConnection disconnect::status:"
             << disconnect(connection, &SocketService::clientDisconnected, this,
                           &NetManager::removeConnection);
    disconnect(connection, &SocketService::MessageReceived, this, &NetManager::MessageReceived);
    connections.removeAt(connections.indexOf(connection));
    connection->finished();
    checkConnectionsStatus();
}

void NetManager::signMessage(IMessage &message) const
{
    message.calcDigSig(accounts->getCurrentActor());
}

QByteArray NetManager::calcHash(Messages::IMessage &message) const
{
    return Utils::calcKeccak(message.serialize());
}

// bool NetManager::addResponseHandler(const QByteArray &message, const QByteArray &msgType)
//{
//    QByteArray hash = Utils::calcKeccak(message);
//    if (Messages::RESPONSE.contains(msgType))
//    {
//        requestResponseMap->insert(hash, Config::Net::NECESSARY_RESPONSE_COUNT);
//        return true;
//    }
//    else
//    {
//        return false;
//    }

//    //    FileList responseHandler;
//    //    QFile file(".responseHamdler");
//    //    responseHandler.setFileList(file);
//    //    if (Messages::RESPONSE.contains(msgType))
//    //    {
//    //        responseHandler.add(message.hash(), message.serialize());
//    //        return true;
//    //    }
//    //    else
//    //        return false;
//}

// bool NetManager::checkResponseHandler(const QByteArray &message)
//{
//    QByteArray hash = Utils::calcKeccak(message);
//    if (requestResponseMap->keys().contains(hash))
//    {
//        int t = requestResponseMap->value(hash) - 1;
//        if (t <= 0)
//        {
//            requestResponseMap->remove(hash);
//        }
//        return true;
//    }
//    else
//    {
//        return false;
//    }
//}

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
            connect(connections.last(), &SocketService::MessageReceived, this, &NetManager::MessageReceived);
            connect(connections.last(), &SocketService::removeMe, this, &NetManager::removeConnection);
            connect(connections.last(), &SocketService::checkMe, this, &NetManager::checkMyIdentificator);
        }
    }
}

// Send messages //
// void NetManager::sendReserveActorRequest(QString peerAddress, QByteArray requestHash, const int port)
//{
//    for (auto i : reservedActorList)
//        qDebug() << i;

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

    sendMessage(genBlock->serialize(), Messages::GENESIS_BLOCK_MESSAGE);
    //    EntityMessage<Block> msg = Messages::createGenesisBlockMessage(*genBlock);

    delete genBlock;
    QFile::remove(DataStorage::TMP_GENESIS_BLOCK);
}

// Send messages //

// void NetManager::sendGetActor(BigNumber actorId)
//{
//    qDebug() << "NET MANAGER: Requesting actor with id =" << actorId;
//    GetActorMessage msg(actorId);
//    //    signMessage(msg);
//    getActorsHandlers.insert(calcHash(msg), GetEntityHandler<Actor<KeyPublic>>());
//    broadcastMsg(msg.serialize());
//}

// void NetManager::shareContract(Contract contract)
//{
//    //    if (contract.makeFirstTransction()) {
//    //        emit contractFirstTransaction(contract);
//    //        return;
//    //    }
//    qDebug() << contract.serialize();
//    if (contract.makeFinalTransaction())
//    {
//        //        emit contractFinalTransaction(contract);
//        return;
//    }
//    //    sendMessage(contract.serialize(), Messages::CONTRACT_MESSAGE);
//}

void NetManager::sendMessageTo(BigNumber recipientId, QByteArray message)
{
    qDebug() << "NET MANAGER: send message to " << recipientId;
    ChatMessage msg(recipientId, message);
    qDebug() << msg.serialize();
    signMessage(msg);
    broadcastMsg(msg.serialize());
}
