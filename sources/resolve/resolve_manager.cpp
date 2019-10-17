#include "headers/resolve/resolve_manager.h"

ResolveManager::ResolveManager(ActorIndex *actorIndex, Blockchain *blockchain, NetManager *networkManager,
                               TransactionManager *txManager, Dfs *dfs, QObject *parent)
    : QObject(parent)
{
    requestResponseMap = new QMap<QByteArray, int>();
    this->actorIndex = actorIndex;
    this->blockchain = blockchain;
    this->networkManager = networkManager;
    this->txManager = txManager;
    this->dfs = dfs;
}

ResolveManager::~ResolveManager()
{
    emit finished();
}

void ResolveManager::connectSignals(ResolverService *resolver)
{
    //    connect(resolver)
    qDebug() << "NET MANAGER: ResolverService " << resolvers.indexOf(resolver) << " connections setup";
    connect(resolver, &ResolverService::TaskFinished, this, &ResolveManager::taskFinished);
    connect(resolver, &ResolverService::responseReady, networkManager, &NetManager::sendMessageResponse);
    connect(resolver, &ResolverService::coinRequest, this, &ResolveManager::coinRequest);
    // "New" signals
    connect(resolver, &ResolverService::newActor, actorIndex, &ActorIndex::handleNewActor);
    connect(resolver, &ResolverService::newBlock, blockchain, &Blockchain::addBlockToBlockchain);
    connect(resolver, &ResolverService::newTx, txManager, &TransactionManager::addTransaction);
    connect(resolver, &ResolverService::newProfile, actorIndex, &ActorIndex::saveProfileFromNetwork);
    connect(resolver, &ResolverService::newDfsPack, dfs, &Dfs::recieve);
    // request signals
    connect(resolver, &ResolverService::getActor, actorIndex, &ActorIndex::handleGetActor);
    connect(resolver, &ResolverService::getActorsCount, actorIndex, &ActorIndex::getActorCount);
    connect(resolver, &ResolverService::getTx, blockchain, &Blockchain::getTxFromBlockchain);
    connect(resolver, &ResolverService::getBlock, blockchain, &Blockchain::getBlockFromBlockchain);
    connect(resolver, &ResolverService::getBlocksCount, blockchain, &Blockchain::getBlockCount);
    // response signals
    connect(actorIndex, &ActorIndex::responseReady, resolver, &ResolverService::responseReady);
    connect(blockchain, &Blockchain::responseReady, resolver, &ResolverService::responseReady);
    connect(resolver, &ResolverService::blockCount, blockchain, &Blockchain::blockCountResponse);
}

void ResolveManager::disconnectSignals(ResolverService *resolver)
{
    //    disconnect(resolver)
    qDebug() << "NET MANAGER: ResolverService " << resolvers.indexOf(resolver) << " connections aborted";
    disconnect(resolver, &ResolverService::TaskFinished, this, &ResolveManager::taskFinished);
    // "New" signals
    disconnect(resolver, &ResolverService::newActor, actorIndex, &ActorIndex::handleNewActor);
    disconnect(resolver, &ResolverService::newBlock, blockchain, &Blockchain::addBlockToBlockchain);
    disconnect(resolver, &ResolverService::newTx, txManager, &TransactionManager::addTransaction);
    disconnect(resolver, &ResolverService::newDfsPack, dfs, &Dfs::recieve);

    // request signals
    disconnect(resolver, &ResolverService::getActor, actorIndex, &ActorIndex::handleGetActor);
    disconnect(resolver, &ResolverService::getTx, blockchain, &Blockchain::getTxFromBlockchain);
    disconnect(resolver, &ResolverService::getBlock, blockchain, &Blockchain::getBlockFromBlockchain);
    disconnect(resolver, &ResolverService::getBlocksCount, blockchain, &Blockchain::getBlockCount);
    disconnect(resolver, &ResolverService::getActorsCount, actorIndex, &ActorIndex::getActorCount);
    // response signals
    disconnect(actorIndex, &ActorIndex::responseReady, resolver, &ResolverService::responseReady);
    disconnect(blockchain, &Blockchain::responseReady, resolver, &ResolverService::responseReady);
}

const QByteArray ResolveManager::calcKeccak256(const QByteArray &msg) const
{
    return Utils::calcKeccak(msg);
}

void ResolveManager::setTask(QByteArray msg, const SocketPair &receiver)
{
    resolvers.append(new ResolverService(actorIndex, requestResponseMap));
    connectSignals(resolvers.last());
    resolvers.last()->setTask(msg, receiver);
    ThreadPool::addThread(resolvers.last());
}

void ResolveManager::registrateMsg(const QByteArray &msg, const QByteArray &msgType)
{
    handlerFileMutex.lock();
    requestResponseMap->insert(calcKeccak256(msg), Config::Net::NECESSARY_RESPONSE_COUNT);
    handlerFileMutex.unlock();
    emit sendMsg(msg, msgType);
}

void ResolveManager::taskFinished()
{
    ResolverService *resolver = qobject_cast<ResolverService *>(QObject::sender());
    disconnectSignals(resolver);
    resolvers.removeOne(resolver);
    emit resolver->finished();
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

void ResolveManager::resolveMessage(const QByteArray &msg, const SocketPair &receiver)
{

    setTask(msg, receiver);
}
