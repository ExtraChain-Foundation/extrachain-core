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
    connect(resolver, &ResolverService::MessageReady, networkManager, &NetManager::sendMessage);
    connect(resolver, &ResolverService::secondWave, networkManager, &NetManager::broadcastMsg);
    // "New" signals
    connect(resolver, &ResolverService::newActor, actorIndex, &ActorIndex::handleNewActor);
    connect(resolver, &ResolverService::newBlock, blockchain, &Blockchain::addBlockToBlockchain);
    connect(resolver, &ResolverService::newTx, txManager, &TransactionManager::addTransaction);
    connect(resolver, &ResolverService::newDfsPack, dfs, &Dfs::recieve);
    // request signals
    connect(resolver, &ResolverService::getActor, actorIndex, &ActorIndex::handleGetActor);
    connect(actorIndex, &ActorIndex::getActorResponse, resolver, &ResolverService::getActorResponse);
    //    connect(resolver, &ResolverService::secondWave, networkManager, &NetManager::broadcastMsg);

    //    connect(resolver, &ResolverService::SendGetActor, this, &NetManager::sendGetActor);

    //    connect(resolver, &ResolverService::newDfsPack, networkManager, &NetManager::newDfsPack);

    //    connect(resolver, &ResolverService::receiveProfile, networkManager, &NetManager::receiveProfile);
    //    connect(networkManager, &NetManager::receiveProfile, actorIndex,
    //    &ActorIndex::saveProfileFromNetwork);

    // spread signals

    // connect(resolver, &ResolverService::getActor, actorIndex, &ActorIndex::getActor);
    //    connect(resolver, &ResolverService::getActorsCount, this, &NetManager::GetActorCount);

    //    connect(resolver, &ResolverService::getBlock, this, &NetManager::GetBlock);
    //    connect(resolver, &ResolverService::getBlocksCount, this, &NetManager::GetBlockCount);
    //    connect(resolver, &ResolverService::NewGenesisBlock, this,
    //    &NetManager::handleNewGenesisBlock);
    //    connect(resolver, &ResolverService::newTx, this, &NetManager::NewTx);
    //    connect(resolver, &ResolverService::getTx, this, &NetManager::GetTx);
    //    connect(resolver, &ResolverService::getTxPair, this, &NetManager::GetTxPair);

    //    connect(resolver, &ResolverService::BlockApproved, this, &NetManager::handleBlockApproved);

    /**
    connect(resolver, &ResolverService::GetActor, this, &NetManager::handleGetActor);
    connect(resolver, &ResolverService::GetTx, this, &NetManager::handleGetTx);
    connect(resolver, &ResolverService::CoinRequest, this, &NetManager::coinRequest);
    connect(resolver, &ResolverService::GetTxPair, this, &NetManager::handleGetTxPair);
    connect(resolver, &ResolverService::GetBlock, this, &NetManager::handleGetBlock);
    connect(resolver, &ResolverService::GetBlockCount, this, &NetManager::handleGetBlockCount);
    connect(resolver, &ResolverService::GetActorCount, this, &NetManager::handleGetActorCount);
    */
    //    connect(this, &NetManager::requestBlockCount, this, &NetManager::sendGetBlockCount);
    //    connect(this, &NetManager::requestActorCount, this, &NetManager::sendGetActorCount);
    /*********************************************************************************************/

    // responses
    /**
    connect(resolver, &ResolverService::GetActorResponse, this, &NetManager::handleGetActorResponse);
    connect(resolver, &ResolverService::GetActorCountResponse, this,
            &NetManager::handleGetActorCountResponse);
    connect(resolver, &ResolverService::GetTxResponse, this, &NetManager::handleGetTxResponse);
    connect(resolver, &ResolverService::GetTxPairResponse, this, &NetManager::handleGetTxPairResponse);
    connect(resolver, &ResolverService::GetBlockResponse, this, &NetManager::handleGetBlockResponse);
    connect(resolver, &ResolverService::GetBlockCountResponse, this,
            &NetManager::handleGetBlockCountResponse);
    */
}

void ResolveManager::disconnectSignals(ResolverService *resolver)
{
    //    disconnect(resolver)
    qDebug() << "NET MANAGER: ResolverService " << resolvers.indexOf(resolver) << " connections setup";
    disconnect(resolver, &ResolverService::TaskFinished, this, &ResolveManager::taskFinished);
    // "New" signals
    disconnect(resolver, &ResolverService::newActor, actorIndex, &ActorIndex::handleNewActor);
    disconnect(resolver, &ResolverService::newBlock, blockchain, &Blockchain::addBlockToBlockchain);
    disconnect(resolver, &ResolverService::newTx, txManager, &TransactionManager::addTransaction);
    // request signals
    disconnect(resolver, &ResolverService::getActor, actorIndex, &ActorIndex::handleGetActor);
}

void ResolveManager::setTask(QByteArray msg, QByteArray hash, QHostAddress senderAddress)
{
    resolvers.append(new ResolverService(actorIndex, requestResponseMap));
    connectSignals(resolvers.last());
    resolvers.last()->setTask(msg, hash, senderAddress);
    ThreadPool::addThread(resolvers.last());
}

void ResolveManager::taskFinished()
{
    ResolverService *resolver = qobject_cast<ResolverService *>(QObject::sender());
    resolvers.removeOne(resolver);
    disconnectSignals(resolver);
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

void ResolveManager::resolveMessage(const QByteArray &msg, const QString &peerAddress, const int port)
{
    setTask(msg, "", QHostAddress(peerAddress));
}
