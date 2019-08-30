#include "extracoin/headers/managers/local_manager.h"

using namespace Messages;

LocalManager::LocalManager(quint16 externalPort, quint16 networkPort)
{
    this->extPort = externalPort;
    this->netPort = networkPort;
}

LocalManager::~LocalManager()
{
//    discoveryService->quit();
    delete resolverService;
    delete upnp;
    delete local;
    delete serverService;
    delete discoveryService;
}
void LocalManager::Verify(const QByteArray &block)
{
    const Block bl(block);
    if (actorIndex->validateBlock(bl))
        emit SendBlockExistence(bl);
    else
        qDebug() << "Error in local manager Verify, Block is not valid";
}
void LocalManager::run()
{
    active = true;
    exec();
}

int LocalManager::exec()
{
    while (isActive())
    {
        //
    }
    return 0;
}

void LocalManager::quit()
{
    active = false;
}

bool LocalManager::isActive() const
{
    return active;
}

// Setup connections //

void LocalManager::setupActorIndexConnections()
{
    // from LocalManager to ActorIndex
    connect(this, &LocalManager::NewActor, actorIndex, &ActorIndex::handleNewActor);
    connect(this, &LocalManager::CheckActorExistence, actorIndex,
            &ActorIndex::handleNewActorCheck);

    // from ActorIndex to LocalManager
    connect(actorIndex, &ActorIndex::ActorIsMissing, this,
            &LocalManager::continueHandlingNewActor);
}

void LocalManager::setupDiscoveryServiceConnections()
{
    connect(discoveryService, &DiscoveryService::ClientDiscovered, this,
            &LocalManager::addConnection);
}

void LocalManager::setupResolverServiceConnections()
{
    connect(resolverService, &ResolverService::SendGetActor, this,
            &LocalManager::sendGetActor);

    // spread signals
    connect(resolverService, &ResolverService::NewActor, this,
            &LocalManager::handleNewActor);
    connect(resolverService, &ResolverService::NewBlock, this,
            &LocalManager::handleNewBlock);
    connect(resolverService, &ResolverService::NewGenesisBlock, this,
            &LocalManager::handleNewGenesisBlock);
    connect(resolverService, &ResolverService::NewTx, this, &LocalManager::handleNewTx);
    connect(resolverService, &ResolverService::BlockApproved, this,
            &LocalManager::handleBlockApproved);
    connect(resolverService, &ResolverService::MergedBlock, this,
            &LocalManager::handleMergedBlock);

    // request signals
    connect(resolverService, &ResolverService::GetActor, this,
            &LocalManager::handleGetActor);
    connect(resolverService, &ResolverService::GetTx, this, &LocalManager::handleGetTx);
    connect(resolverService, &ResolverService::GetTxPair, this,
            &LocalManager::handleGetTxPair);
    connect(resolverService, &ResolverService::GetBlock, this,
            &LocalManager::handleGetBlock);
    connect(resolverService, &ResolverService::GetBlockCount, this,
            &LocalManager::handleGetBlockCount);

    // responses
    connect(resolverService, &ResolverService::GetActorResponse, this,
            &LocalManager::handleGetActorResponse);
    connect(resolverService, &ResolverService::GetTxResponse, this,
            &LocalManager::handleGetTxResponse);
    connect(resolverService, &ResolverService::GetTxPairResponse, this,
            &LocalManager::handleGetTxPairResponse);
    connect(resolverService, &ResolverService::GetBlockResponse, this,
            &LocalManager::handleGetBlockResponse);
    connect(resolverService, &ResolverService::GetBlockCountResponse, this,
            &LocalManager::handleGetBlockCountResponse);
}

// Basic methods

void LocalManager::broadcastMsg(Messages::IMessage &msg)
{
    if (!hasEnoughPeers())
    {
        qWarning() << QString(
                          "Can't broadcast the message (there no enought peers [%1/%2])")
                          .arg(QString::number(connections.size()),
                               QString::number(Config::Net::MINIMUM_PEERS));
        return;
    }

    for (SocketService *connection : connections)
    {
        connection->sendMsg(msg.serialize());
    }
}

void LocalManager::sendMsgToPeer(IMessage &msg, QHostAddress peerAddress)
{
    if (!hasEnoughPeers())
    {
        qWarning() << QString("Can't send the message to %1 (reason - there no enought "
                              "peers [%2/%3])")
                          .arg(peerAddress.toString(),
                               QString::number(connections.size()),
                               QString::number(Config::Net::MINIMUM_PEERS));
        return;
    }

    for (SocketService *connection : connections)
    {
        if (QHostAddress(connection->getAddress()).toIPv4Address()
            == peerAddress.toIPv4Address())
        {
            connection->sendMsg(msg.serialize());
            return;
        }
    }
}

void LocalManager::init(ActorIndex *actorChain, AccountController *accountList)
{
    this->actorIndex = actorChain;
    this->accounts = accountList;
    this->resolverService = new ResolverService(actorIndex);
    findLocal();
    setupResolverServiceConnections();
    if (local != nullptr)
    {
        bool sub =
            local->ip().isInSubnet(QHostAddress(QString("192.168.0.0")),
                                   QHostAddress("255.255.255.255").toIPv4Address());
        setupDiscoveryServiceConnections();
        if (sub)
        {
            upnp = new UPNPConnection(*local);
            QObject::connect(upnp, SIGNAL(success()), this, SLOT(startNetwork()));
            connect(upnp, SIGNAL(upnp_error(QString)), this, SLOT(upnpErr(QString)));
            upnp->makeTunnel(extPort, extPort, "UDP", "Discovery tunnel of ExtraCoin ");
            upnp->makeTunnel(netPort, netPort, "TCP", "Network tunnel of ExtraCoin ");
        }
        else
        {
            startNetwork();
        }
    }
}

void LocalManager::findLocal()
{
    int i = 0, j = 0;
    QNetworkInterface *iface = new QNetworkInterface();
    QList<QNetworkInterface> if_list = iface->allInterfaces();
    QList<QNetworkAddressEntry> addr;
    addr = if_list.at(i).addressEntries();
    while ((local == nullptr) && (addr.size() > 0))
    {
        if ((if_list.at(i).name() != QString("lo")) && (addr.size() > 0))
        {
            while ((j < addr.size()) && (local == nullptr))
            {
                if ((addr.at(j).ip().toString() != QString(""))
                    && (addr.at(j).broadcast().toString() != QString(""))
                    && (addr.at(j).netmask().toString() != QString("")))
                {
                    local = new QNetworkAddressEntry(addr[j]);
                }
                else
                {
                    j++;
                }
            }
            if (addr.size() > 0)
            {
                i++;
            }
        }
        else
        {
            if_list.removeAt(i);
        }
        addr = if_list.at(i).addressEntries();
    }
}

void LocalManager::startNetwork()
{
    this->discoveryService = new DiscoveryService(extPort, netPort);
    if (actorIndex != nullptr && accounts != nullptr)
    {
        serverService = new ServerService(netPort);
//        discoveryService->start();
//        serverService->start();
    }
    else
    {
        qCritical() << "Can't start network: actorIndex or account controller is not set";
    }
}

void LocalManager::upnpErr(QString msg) const
{
    qCritical() << "UPnP Error: " << msg;
}

void LocalManager::addConnection(QString address, quint16 port)
{
    for (SocketService *connection : connections)
    {
        if (connection->getAddress() == address)
        {
            qDebug() << "Can't add connection (already established): " << address;
            return;
        }
    }

    qDebug() << "New connection is established : " << address << ":" << port;
    connections.append(new SocketService(address, port));
    connect(connections.last(), &SocketService::finished, this,
            &LocalManager::removeConnection);
}

void LocalManager::removeConnection()
{
    QObject *sender = QObject::sender();
    SocketService *connection = qobject_cast<SocketService *>(sender);
    disconnect(connection, &SocketService::finished, this,
               &LocalManager::removeConnection);
    int res = connections.indexOf(connection);
    if (res != -1)
    {
        qDebug() << "Connection removed:" << connection->getAddress() << ":"
                 << connection->getPort();
        connections.removeAt(res);
    }
    else
    {
        qWarning() << "Can't remove connection:" << connection->getAddress() << ":"
                   << connection->getPort();
    }
}

// Shortcut methods

bool LocalManager::hasEnoughPeers() const
{
    return connections.size() >= Config::Net::MINIMUM_PEERS;
}

void LocalManager::signMessage(Messages::IMessage &message) const
{
    message.calcDigSig(accounts->getCurrentActor());
}

QByteArray LocalManager::calcHash(Messages::IMessage &message) const
{
    return Utils::calcKeccak(message.serialize());
}

// Send messages //

void LocalManager::sendNewActor(Actor<KeyPublic> actor)
{
    EntityMessage<Actor<KeyPublic>> msg = Messages::createActorMessage(actor);
    signMessage(msg);
    broadcastMsg(msg);
}

void LocalManager::sendNewTx(Transaction tx)
{
    qDebug() << "slot LocalManager::sendNewTx ";
    EntityMessage<Transaction> msg = Messages::createTxMessage(tx);
    signMessage(msg);
    broadcastMsg(msg);
}

void LocalManager::sendNewBlock(Block block)
{
    EntityMessage<Block> msg = Messages::createBlockMessage(block);
    signMessage(msg);
    broadcastMsg(msg);
}

void LocalManager::sendTxResponse(Transaction tx, SearchEnum::TxParam param,
                                  QString value, QHostAddress peerAddress,
                                  QByteArray requestHash)
{
    qDebug() << "Sending tx" << tx.getHash() << "to" << peerAddress.toString();
    EntityResponseMessage<Transaction> msg =
        Messages::createGetTxResponse(tx, requestHash);
    signMessage(msg);
    sendMsgToPeer(msg, peerAddress);
}

void LocalManager::sendTxPairResponse(TxPair pair, QHostAddress peerAddress,
                                      QByteArray requestHash)
{
    qDebug() << "Sending txPair" << pair.serialize() << "to" << peerAddress.toString();
    EntityResponseMessage<TxPair> msg =
        Messages::createGetTxPairResponse(pair, requestHash);
    signMessage(msg);
    sendMsgToPeer(msg, peerAddress);
}

void LocalManager::sendBlockResponse(Block block, SearchEnum::BlockParam param,
                                     QString value, QHostAddress peerAddress,
                                     QByteArray requestHash)
{
    qDebug() << "Sending block" << block.serialize() << "to" << peerAddress.toString();
    EntityResponseMessage<Block> msg =
        Messages::createGetBlockResponse(block, requestHash);
    signMessage(msg);
    sendMsgToPeer(msg, peerAddress);
}

void LocalManager::sendBlockCountResponse(BigNumber blockCount, QHostAddress peerAddress,
                                          QByteArray requestHash)
{
    qDebug() << "Sending block count" << blockCount << "to" << peerAddress.toString();
    EntityResponseMessage<BigNumber> msg =
        createGetBlockCountResponse(blockCount, requestHash);
    signMessage(msg);
    sendMsgToPeer(msg, peerAddress);
}

void LocalManager::sendMergedBlock(Block firstBlock, Block secondBlock, Block resultBlock)
{
    qDebug() << "Spreading merged block" << resultBlock.getIndex();
    MergedBlockMessage msg(firstBlock, secondBlock, resultBlock);
    signMessage(msg);
    broadcastMsg(msg);
}

void LocalManager::sendGenesisBlock(Block prevBlock, QByteArray prevGenHash)
{
    qDebug() << "Sending genesis block";
    GenesisBlock *genBlock = Blockchain::readGenesisBlock(prevBlock, prevGenHash);
    if (genBlock == nullptr)
    {
        qCritical() << "Error while sending genesis block";
        return;
    }

    // sign block
    genBlock->sign(accounts->getCurrentActor());

    EntityMessage<Block> msg = Messages::createGenesisBlockMessage(*genBlock);

    delete genBlock;
    QFile::remove(DataStorage::TMP_GENESIS_BLOCK);

    signMessage(msg);
    broadcastMsg(msg);
}

// Send messages //

void LocalManager::sendGetActor(BigNumber actorId)
{
    qDebug() << "Requesting actor with id =" << actorId;
    GetActorMessage msg(actorId);
    signMessage(msg);
    getActorsHandlers.insert(calcHash(msg), GetEntityHandler<Actor<KeyPublic>>());
    broadcastMsg(msg);
}

void LocalManager::sendGetBlock(BlockParam param, QString value)
{
    qDebug() << "Requesting block by" << toString(param) << "and" << value;
    GetBlockMessage msg(param, value.toLocal8Bit());
    signMessage(msg);
    getBlockHandlers.insert(calcHash(msg), GetEntityHandler<Block>());
    broadcastMsg(msg);
}

void LocalManager::sendGetBlockCount()
{
    qDebug() << "Requesting block count";
    BaseMessage msg = Messages::createGetBlockCountMessage();
    signMessage(msg);
    getCountHandlers.insert(calcHash(msg), GetCountHandler());
    broadcastMsg(msg);
}

void LocalManager::sendGetTx(TxParam param, QString value)
{
    qDebug() << "Requesting tx by" << toString(param) << "and" << value;
    GetTxMessage msg(param, value.toLocal8Bit());
    signMessage(msg);
    getTxHandlers.insert(calcHash(msg), GetEntityHandler<Transaction>());
    broadcastMsg(msg);
}

void LocalManager::sendGetTxPair(BigNumber sender, BigNumber receiver)
{
    qDebug() << "Requesting tx pair. Sender:" << sender << ", Receiver:" << receiver;
    GetTxPairMessage msg(sender, receiver);
    signMessage(msg);
    getTxPairHandlers.insert(calcHash(msg), GetEntityHandler<TxPair>());
    broadcastMsg(msg);
}

// Handling messsages ///

void LocalManager::handleNewActor(Actor<KeyPublic> actor, QHostAddress peerAddress)
{
    qDebug() << "Handling NewActor" << actor.toString() << "from"
             << peerAddress.toString();
    emit NewActor(actor);
}

void LocalManager::continueHandlingNewActor(Actor<KeyPublic> actor)
{
    EntityMessage<Actor<KeyPublic>> msg = Messages::createActorMessage(actor);
    signMessage(msg);
    broadcastMsg(msg);
}

void LocalManager::handleNewBlock(Block block, QHostAddress peerAddress)
{
    qDebug() << "Handling NewBlock" << block.toString() << "from"
             << peerAddress.toString();
    emit CheckBlockExistence(block);
}

void LocalManager::continueHandlingNewBlock(Block block)
{
    sendNewBlock(block);
}

void LocalManager::handleNewGenesisBlock(Block block, QHostAddress peerAddress)
{
    qDebug() << "Handling NewGenesisBlock" << block.toString() << "from"
             << peerAddress.toString();

    emit AddBlock(block);

    EntityMessage<Block> msg = Messages::createGenesisBlockMessage(block);
    signMessage(msg);
    broadcastMsg(msg);
}

void LocalManager::handleNewTx(Transaction tx, QHostAddress peerAddress)
{
    qDebug() << "Handling newTx" << tx.toString() << "from" << peerAddress.toString();

    // If there are hops -> spread message forward
    if (tx.getHop() > 0)
    {
        tx.decrementHop();

        EntityMessage<Transaction> msg = Messages::createTxMessage(tx);
        signMessage(msg);
        broadcastMsg(msg);
    }

    emit NewTx(tx);
}

void LocalManager::handleBlockApproved(BigNumber blockId, BigNumber approver,
                                       QHostAddress peerAddress)
{
    qDebug() << "Handling BlockApproved" << blockId << "from" << peerAddress.toString();
    emit BlockApproved(blockId, approver, peerAddress);
}

void LocalManager::handleMergedBlock(Block first, Block second, Block result,
                                     QByteArray dsig, QHostAddress peerAddress)
{
    qDebug() << "Handling MergedBlock" << result.getIndex() << "from"
             << peerAddress.toString();
    emit HandleMergedBlock(first, second, result, dsig, peerAddress);
}

void LocalManager::handleGetActor(BigNumber actorId, QHostAddress peerAddress,
                                  QByteArray requestHash)
{
    qDebug() << "Handling request: getActor" << actorId << "from"
             << peerAddress.toString();
    Actor<KeyPublic> actor = actorIndex->getActor(actorId);
    if (actor.isEmpty())
    {
        qDebug() << "Can't handle request: There no actor with id" << actorId
                 << "locally";
        return;
    }

    EntityResponseMessage<Actor<KeyPublic>> msg =
        Messages::createGetActorResponse(actor, requestHash);
    signMessage(msg);
    sendMsgToPeer(msg, peerAddress);
}

void LocalManager::handleGetTx(TxParam param, QByteArray value, QHostAddress peerAddress,
                               QByteArray requestHash)
{
    qDebug() << "Handling request: getTx" << toString(param) << value << "from"
             << peerAddress.toString();
    emit GetTx(param, value, peerAddress, requestHash);
}

void LocalManager::handleGetTxPair(BigNumber sender, BigNumber receiver,
                                   QHostAddress peerAddress, QByteArray requestHash)
{
    qDebug() << "Handling request: getTxPair sender=" << sender << "receiver=" << receiver
             << "from" << peerAddress.toString();
    emit GetTxPair(sender, receiver, peerAddress, requestHash);
}

void LocalManager::handleGetBlock(BlockParam param, QByteArray value,
                                  QHostAddress peerAddress, QByteArray requestHash)
{
    qDebug() << "Handling request: getBlock" << toString(param) << value << "from"
             << peerAddress.toString();
    emit GetBlock(param, value, peerAddress, requestHash);
}

void LocalManager::handleGetBlockCount(QHostAddress peerAddress, QByteArray requestHash)
{
    qDebug() << "Handling request: getBlockCount from" << peerAddress.toString();
    emit GetBlockCount(peerAddress, requestHash);
}

void LocalManager::handleGetActorResponse(Actor<KeyPublic> actor, QByteArray reqHash,
                                          QHostAddress peerAddress)
{
    // if handler doesn't exists
    if (!getActorsHandlers.contains(reqHash))
    {
        qDebug() << "Error: not waiting for getActor responses with reqHash=" << reqHash;
        return;
    }

    qDebug() << "Handling response: actor" << actor.getId() << "from"
             << peerAddress.toString();
    GetEntityHandler<Actor<KeyPublic>> handler = getActorsHandlers[reqHash];
    handler.addResponse(actor);
    getActorsHandlers.insert(reqHash, handler);

    if (handler.canProcess())
    {
        Actor<KeyPublic> toAdd = handler.resolveBestEntity();
        if (!toAdd.isEmpty())
        {
            qDebug() << "Adding new Actor" << toAdd.toString();
            emit NewActor(actor);
            // clear handler
            getTxHandlers.remove(reqHash);
            return;
        }
        else
        {
            // if we have controversial situation - wait for some more requests
            qDebug() << "Can't resolve best Actor entity";
        }
    }

    qDebug() << "Waiting for more GetActor [" << reqHash << "] responses";
}

void LocalManager::handleGetTxResponse(Transaction tx, QByteArray reqHash,
                                       QHostAddress peerAddress)
{
    // if handler doesn't exists
    if (!getTxHandlers.contains(reqHash))
    {
        qDebug() << "Error: not waiting for getTx responses with reqHash=" << reqHash;
        return;
    }

    qDebug() << "Handling response: tranaction" << tx.getHash() << "from"
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
                qDebug() << "Adding new Tx" << toAdd.toString();
                emit TxResponse(tx, peerAddress);
                // clear handler
                getTxHandlers.remove(reqHash);
                return;
            }
            else
            {
                qDebug() << "Warning: Received tx" << toAdd.toString() << "is not valid.";
            }
        }
        else
        {
            // if we have controversial situation - wait for some more requests
            qDebug() << "Can't resolve best Transaction entity";
        }
    }

    qDebug() << "Waiting for more GetTx [" << reqHash << "] responses";
}

void LocalManager::handleGetTxPairResponse(TxPair pair, QByteArray reqHash,
                                           QHostAddress peerAddress)
{
    // if handler doesn't exists
    if (!getTxHandlers.contains(reqHash))
    {
        qDebug() << "Error: not waiting for getTx responses with reqHash=" << reqHash;
        return;
    }

    qDebug() << "Handling response: txPair" << pair.serialize() << "from"
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
            if (actorIndex->validateTx(pair.getFirst())
                && actorIndex->validateTx(pair.getSecond()))
            {
                qDebug() << "Adding new TxPair" << toAdd.serialize();
                emit TxPairResponse(pair, peerAddress);
                // clear handler
                getTxHandlers.remove(reqHash);
                return;
            }
            else
            {
                qWarning() << "Warning: Received TxPair" << toAdd.serialize()
                           << "is not valid.";
            }
        }
        else
        {
            // if we have controversial situation - wait for some more requests
            qWarning() << "Can't resolve best TxPair entity";
        }
    }

    qDebug() << "Waiting for more GetTxPair [" << reqHash << "] responses";
}

void LocalManager::handleGetBlockResponse(Block block, QByteArray reqHash,
                                          QHostAddress peerAddress)
{
    // if handler doesn't exists
    if (!getBlockHandlers.contains(reqHash))
    {
        qDebug() << "Error: not waiting for getBlock responses with reqHash=" << reqHash;
        return;
    }

    qDebug() << "Handling response: block" << block.getIndex() << "from"
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
                qDebug() << "Adding new Block" << toAdd.toString();
                emit AddBlock(toAdd);
                // clear handler
                getBlockHandlers.remove(reqHash);
                return;
            }
            else
            {
                qDebug() << "Warning: Received block" << toAdd.toString()
                         << "is not valid.";
            }
        }
        else
        {
            // if we have controversial situation - wait for some more requests
            qDebug() << "Can't resolve best block entity";
        }
    }

    qDebug() << "Waiting for more GetBlock [" << reqHash << "] responses";
}

void LocalManager::handleGetBlockCountResponse(BigNumber blockCount, QByteArray reqHash,
                                               QHostAddress peerAddress)
{
    // if handler doesn't exists
    if (!getCountHandlers.contains(reqHash))
    {
        qDebug() << "Error: not waiting for block count responses with reqHash="
                 << reqHash;
        return;
    }

    qDebug() << "Handling response: block count" << blockCount << "from"
             << peerAddress.toString();
    GetCountHandler handler = getCountHandlers[reqHash];
    handler.addResponse(blockCount);
    getCountHandlers.insert(reqHash, handler);

    if (handler.canProcess())
    {
        BigNumber searchedValue = handler.getSearchedValue();
        this->maxBlockCount = searchedValue;
        qDebug() << "Max block count is set to" << searchedValue;
        emit BlockCountResponse(searchedValue, peerAddress);

        // clear handler
        getCountHandlers.remove(reqHash);
    }

    qDebug() << "Waiting for more GetBlockCount [" << reqHash << "] responses";
}
