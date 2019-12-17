#include "resolve/resolve_manager.h"

void ResolveManager::setNode(NodeManager *value)
{
    node = value;
}

void ResolveManager::setChatManager(ChatManager *value)
{
    chatManager = value;
}

QMap<QByteArray, int> *ResolveManager::getRequestResponseMap() const
{
    return requestResponseMap;
}

ResolveManager::ResolveManager(ActorIndex *actorIndex, Blockchain *blockchain, NetManager *networkManager,
                               TransactionManager *txManager, AccountController *accountControler,
                               QObject *parent)
    : QObject(parent)
{
    requestResponseMap = new QMap<QByteArray, int>();
    this->actorIndex = actorIndex;
    this->blockchain = blockchain;
    this->networkManager = networkManager;
    this->txManager = txManager;
    this->accountControler = accountControler;

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
    connect(resolver, &ResolverService::TaskFinished, this, &ResolveManager::taskFinished);
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
    disconnect(resolver, &ResolverService::TaskFinished, this, &ResolveManager::taskFinished);
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
}

const QByteArray ResolveManager::calcKeccak256(const QByteArray &msg) const
{
    return Utils::calcKeccak(msg);
}

void ResolveManager::createNewResolver(const Network::DataStruct &task)
{
    l1Res.append(new ResolverService(Resolver::Type::GENERAL, Resolver::Lifetime::SHORT, actorIndex, this));
    l1Res.last()->setNode(node);
    l1Res.last()->setBlockchain(blockchain);
    l1Res.last()->setChatManager(chatManager);
    connectSignals(l1Res.last());
    // get task from queue
    l1Res.last()->setTask(task.msg, task.receiver);
    ThreadPool::addThread(l1Res.last());
}

bool ResolveManager::setTask(QByteArray msg, const SocketPair &receiver)
{
    Network::DataStruct task;
    task.msg = msg;
    task.receiver = receiver;
    mutex.lock();
    if (this == nullptr)
        return false;
    this->unprocessed.push(task);
    bool lockRes = popUnprocces();
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
        msg.calcDigSig(*accountControler->getMainActor());
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
        rmsg.calcDigSig(*accountControler->getMainActor());

    //    qDebug() << "NetManager: send " << msgType;
    networkManager->distMessage(rmsg.serialize(), receiver);
    //    emit socketSendMsg(rmsg.serialize(), receiver);
}

void ResolveManager::taskFinished()
{
    ResolverService *resolver = qobject_cast<ResolverService *>(QObject::sender());
    disconnectSignals(resolver);
    if (resolver->getType() == Resolver::Type::DFS)
    {
        if (resolver != nullptr)
            emit resolver->finished();
        return;
    }
    else if (resolver->getType() == Resolver::Type::GENERAL)
    {
        l1Res.removeOne(resolver);
        if (resolver != nullptr)
            emit resolver->finished();
        if (unprocessed.size() != 0)
        {
            mutex.lock();
            popUnprocces();
            mutex.unlock();
        }
        return;
    }
}

void ResolveManager::process()
{
}

QList<ResolverService *> ResolveManager::getActive()
{
    QList<ResolverService *> ret;
    foreach (ResolverService *resolver, l1Res)
    {
        if (resolver->isActive())
            ret.append(resolver);
    }
    return ret;
}
QList<ResolverService *> ResolveManager::getFinished()
{
    QList<ResolverService *> ret;
    foreach (ResolverService *resolver, l1Res)
    {
        if (!resolver->isActive())
            ret.append(resolver);
    }
    return ret;
}

bool ResolveManager::popUnprocces()
{
    bool res = false;
    while (l1Res.size() < ResolverServicePoolMaxSize && !unprocessed.empty())
    {
        createNewResolver(unprocessed.front());
        unprocessed.pop();
        res = true;
    }
    return res;
}
