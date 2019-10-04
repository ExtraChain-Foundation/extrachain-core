#include "headers/resolve/resolve_manager.h"

ResolveManager::ResolveManager(ActorIndex *actorIndex, Blockchain *blockchain, NetManager *networkManager,
                               QObject *parent)
{
    requestResponseMap = new QMap<QByteArray, int>();
    this->actorIndex = actorIndex;
    this->blockchain = blockchain;
    this->networkManager = networkManager;
}

ResolveManager::~ResolveManager()
{
    //
}

void ResolveManager::connectSignals(ResolverService *resolver)
{
    //    connect(resolver)
    qDebug() << "NET MANAGER: setupResolverServiceConnections";

    //    connect(resolver, &ResolverService::secondWave, networkManager, &NetManager::broadcastMsg);

    //    connect(resolver, &ResolverService::SendGetActor, this, &NetManager::sendGetActor);

    connect(resolver, &ResolverService::newDfsPack, this, &NetManager::newDfsPack);

    connect(resolver, &ResolverService::receiveProfile, this, &NetManager::receiveProfile);
    connect(this, &NetManager::receiveProfile, actorIndex, &ActorIndex::saveProfileFromNetwork);

    // spread signals

    connect(resolver, &ResolverService::newActor, actorIndex, &ActorIndex::handleNewActor);
    connect(resolver, &ResolverService::getActor, actorIndex, &ActorIndex::getActor);
    connect(resolver, &ResolverService::getActorsCount, this, &NetManager::GetActorCount);

    connect(resolver, &ResolverService::newBlock, this, &NetManager::AddBlock);
    connect(resolver, &ResolverService::getBlock, this, &NetManager::GetBlock);
    connect(resolver, &ResolverService::getBlocksCount, this, &NetManager::GetBlockCount);
    //    connect(resolver, &ResolverService::NewGenesisBlock, this,
    //    &NetManager::handleNewGenesisBlock);
    connect(resolver, &ResolverService::newTx, this, &NetManager::NewTx);
    connect(resolver, &ResolverService::getTx, this, &NetManager::GetTx);
    connect(resolver, &ResolverService::getTxPair, this, &NetManager::GetTxPair);

    //    connect(resolver, &ResolverService::BlockApproved, this, &NetManager::handleBlockApproved);

    // request signals
    connect(resolver, &ResolverService::getActor, actorIndex, &ActorIndex::handleGetActor);
    /**
    connect(resolver, &ResolverService::GetActor, this, &NetManager::handleGetActor);
    connect(resolver, &ResolverService::GetTx, this, &NetManager::handleGetTx);
    connect(resolver, &ResolverService::CoinRequest, this, &NetManager::coinRequest);
    connect(resolver, &ResolverService::GetTxPair, this, &NetManager::handleGetTxPair);
    connect(resolver, &ResolverService::GetBlock, this, &NetManager::handleGetBlock);
    connect(resolver, &ResolverService::GetBlockCount, this, &NetManager::handleGetBlockCount);
    connect(resolver, &ResolverService::GetActorCount, this, &NetManager::handleGetActorCount);
    */
    connect(this, &NetManager::requestBlockCount, this, &NetManager::sendGetBlockCount);
    connect(this, &NetManager::requestActorCount, this, &NetManager::sendGetActorCount);
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
}

void ResolveManager::setTask(QByteArray msg, QByteArray hash, QHostAddress senderAddress)
{
    resolvers.append(new ResolverService(actorIndex, requestResponseMap));
    connect(resolvers[0], &ResolverService::TaskFinished, this, &ResolveManager::taskFinised);
}

void ResolveManager::taskFinised()
{
    ResolverService *resolver = qobject_cast<ResolverService *>(QObject::sender());
    resolvers.removeAt(resolvers.indexOf(resolver));
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
