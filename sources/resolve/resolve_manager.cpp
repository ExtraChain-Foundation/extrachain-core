#include "headers/resolve/resolve_manager.h"

void ResolveManager::setNode(NodeManager *value)
{
    node = value;
}

void ResolveManager::setChatManager(ChatManager *value)
{
    chatManager = value;
}

ResolveManager::ResolveManager(ActorIndex *actorIndex, Blockchain *blockchain, NetManager *networkManager,
                               TransactionManager *txManager, AccountController *accountControler, Dfs *dfs,
                               QObject *parent)
    : QObject(parent)
{
    requestResponseMap = new QMap<QByteArray, int>();
    this->actorIndex = actorIndex;
    this->blockchain = blockchain;
    this->networkManager = networkManager;
    this->txManager = txManager;
    this->accountControler = accountControler;
    this->dfs = dfs;

    //    connect(this, &ResolveManager::socketSendMsg, networkManager, &NetManager::sendMsg);
    connect(actorIndex, &ActorIndex::responseReady, this, &ResolveManager::sendMessageResponse);
    connect(blockchain, &Blockchain::responseReady, this, &ResolveManager::sendMessageResponse);
}

ResolveManager::~ResolveManager()
{

    //    disconnect(this, &ResolveManager::socketSendMsg, networkManager, &NetManager::sendMsg);
    disconnect(actorIndex, &ActorIndex::responseReady, this, &ResolveManager::sendMessageResponse);
    disconnect(blockchain, &Blockchain::responseReady, this, &ResolveManager::sendMessageResponse);
    emit finished();
}

void ResolveManager::connectSignals(ResolverService *resolver)
{
    //    connect(resolver)
    qDebug() << "NET MANAGER: ResolverService " << resolvers.indexOf(resolver) << " connections setup";
    connect(resolver, &ResolverService::TaskFinished, this, &ResolveManager::taskFinished);
    //    connect(resolver, &ResolverService::coinRequest, this, &ResolveManager::coinRequest);
    // "New" signals
    //    connect(resolver, &ResolverService::newActor, actorIndex, &ActorIndex::handleNewActor);
    //    connect(resolver, &ResolverService::newBlock, blockchain, &Blockchain::addBlockToBlockchain);
    connect(resolver, &ResolverService::newGenesisBlock, blockchain, &Blockchain::addGenBlockToBlockchain);
    connect(resolver, &ResolverService::newTx, txManager, &TransactionManager::addTransaction);
    connect(resolver, &ResolverService::newProfile, actorIndex, &ActorIndex::saveProfileFromNetwork);
    // request signals
    connect(resolver, &ResolverService::getActor, actorIndex, &ActorIndex::handleGetActor);
    connect(resolver, &ResolverService::handleGetAllActor, actorIndex, &ActorIndex::handleGetAllActor);
    connect(resolver, &ResolverService::getActorsCount, actorIndex, &ActorIndex::getActorCount);
    connect(resolver, &ResolverService::getTx, blockchain, &Blockchain::getTxFromBlockchain);
    connect(resolver, &ResolverService::getBlock, blockchain, &Blockchain::getBlockFromBlockchain);
    connect(resolver, &ResolverService::getBlocksCount, blockchain, &Blockchain::getBlockCount);
    // response signals
    connect(resolver, &ResolverService::blockCount, blockchain, &Blockchain::blockCountResponse);
    // dfs signal
    //    connect(resolver, &ResolverService::dfsMessage, dfs, &Dfs::resolveMsg);
}

void ResolveManager::disconnectSignals(ResolverService *resolver)
{
    //    disconnect(resolver)
    qDebug() << "NET MANAGER: ResolverService " << resolvers.indexOf(resolver) << " connections aborted";
    disconnect(resolver, &ResolverService::TaskFinished, this, &ResolveManager::taskFinished);
    // "New" signals
    //    disconnect(resolver, &ResolverService::newActor, actorIndex, &ActorIndex::handleNewActor);
    //    disconnect(resolver, &ResolverService::newBlock, blockchain, &Blockchain::addBlockToBlockchain);
    disconnect(resolver, &ResolverService::newTx, txManager, &TransactionManager::addTransaction);

    // request signals
    disconnect(resolver, &ResolverService::getActor, actorIndex, &ActorIndex::handleGetActor);
    disconnect(resolver, &ResolverService::handleGetAllActor, actorIndex, &ActorIndex::handleGetAllActor);
    disconnect(resolver, &ResolverService::getTx, blockchain, &Blockchain::getTxFromBlockchain);
    disconnect(resolver, &ResolverService::getBlock, blockchain, &Blockchain::getBlockFromBlockchain);
    disconnect(resolver, &ResolverService::getBlocksCount, blockchain, &Blockchain::getBlockCount);
    disconnect(resolver, &ResolverService::getActorsCount, actorIndex, &ActorIndex::getActorCount);
    // response signals
    disconnect(resolver, &ResolverService::blockCount, blockchain, &Blockchain::blockCountResponse);
    // dfs signal
    //    disconnect(resolver, &ResolverService::dfsMessage, dfs, &Dfs::resolveMsg);
}

const QByteArray ResolveManager::calcKeccak256(const QByteArray &msg) const
{
    return Utils::calcKeccak(msg);
}

void ResolveManager::createNewResolver(const DataStruct &task)
{
    resolvers.append(new ResolverService(actorIndex, requestResponseMap, listFile, fileMap, pckgCounter));
    resolvers.last()->setNode(node);
    resolvers.last()->setBlockchain(blockchain);
    resolvers.last()->setDfs(dfs);
    resolvers.last()->setChatManager(chatManager);
    connectSignals(resolvers.last());
    // get task from queue
    resolvers.last()->setTask(task.msg, task.receiver);
    auto crutch = resolvers.last();
    connect(resolvers.last(), &ResolverService::finished, [crutch]() { crutch->thread()->exit(); });
    ThreadPool::addThread(resolvers.last());
}

bool ResolveManager::setTask(QByteArray msg, const SocketPair &receiver)
{
    DataStruct task;
    task.msg = msg;
    task.receiver = receiver;
    mutex.lock();
    unprocessed.push(task);
    bool lockRes = resolvers.size() < ResolverServicePoolMaxSize;
    if (lockRes)
    {
        DataStruct currentTask = unprocessed.front();
        unprocessed.pop();
        createNewResolver(currentTask);
    }
    mutex.unlock();
    return lockRes;
}

void ResolveManager::registrateMsg(const QByteArray &data, const QByteArray &msgType)
{

    Messages::BaseMessage msg(msgType);
    msg.init(data);

    if (msgType != Messages::ACTOR_MESSAGE)
    {
        if (accountControler->getAccountCount() == 0)
            return;
        msg.calcDigSig(accountControler->getCurrentActor());
    }
    //    qDebug() << "msg signature:" << msg.getDigSig();

    //    qDebug() << "send " << msgType;
    QByteArray message = msg.serialize();
    if (Messages::GETTERS.contains(msgType))
    {
        handlerFileMutex.lock();
        requestResponseMap->insert(calcKeccak256(message), Config::Net::NECESSARY_RESPONSE_COUNT);
        handlerFileMutex.unlock();
    }
    networkManager->broadcastMsg(message);
    //    emit sendMsg(message);
}

void ResolveManager::sendMessageResponse(const QByteArray &data, const QByteArray &msgType,
                                         const QByteArray &requestHash, const SocketPair &receiver)

{
    Messages::BaseMessageResponse rmsg(data, requestHash, msgType);
    if (msgType != Messages::GET_ACTOR_RESPONSE_MESSAGE
        /*&& msgType != Messages::GET_ALL_ACTORS_RESPONSE_MESSAGE*/)
        rmsg.calcDigSig(accountControler->getCurrentActor());

    qDebug() << "NetManager: send " << msgType;
    networkManager->distMessage(rmsg.serialize(), receiver);
    //    emit socketSendMsg(rmsg.serialize(), receiver);
}

void ResolveManager::taskFinished()
{
    ResolverService *resolver = qobject_cast<ResolverService *>(QObject::sender());
    disconnectSignals(resolver);
    resolvers.removeOne(resolver);
    emit resolver->finished();
    if ((resolvers.size() == 0) && (unprocessed.size() != 0))
    {
        createNewResolver(unprocessed.front());
        unprocessed.pop();
    }
}

void ResolveManager::process()
{
    //
}

QList<ResolverService *> ResolveManager::getActive()
{
    QList<ResolverService *> ret;
    foreach (ResolverService *resolver, resolvers)
    {
        if (resolver->isActive())
            ret.append(resolver);
    }
    return ret;
}
QList<ResolverService *> ResolveManager::getFinished()
{
    QList<ResolverService *> ret;
    foreach (ResolverService *resolver, resolvers)
    {
        if (!resolver->isActive())
            ret.append(resolver);
    }
    return ret;
}

// void ResolveManager::resolveMessage(const QByteArray &msg, const SocketPair &receiver)
//{

//    setTask(msg, receiver);
//}
