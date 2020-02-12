#include "resolve/resolver_service.h"
#include "resolve/resolve_manager.h"
#include "managers/node_manager.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/blockchain.h"
#include "managers/tx_manager.h"
#include "dfs/controls/headers/dfs.h"
#include "managers/chatmanager.h"
#include "managers/account_controller.h"

void ResolverService::setNode(NodeManager *value)
{
    node = value;
}

void ResolverService::setBlockchain(Blockchain *value)
{
    blockchain = value;
}

void ResolverService::setChatManager(ChatManager *value)
{
    chatManager = value;
}

void ResolverService::setResolveManager(ResolveManager *value)
{
    resolveManager = value;
}

Resolver::Type ResolverService::getType() const
{
    return type;
}

void ResolverService::setType(const Resolver::Type &value)
{
    type = value;
}

Resolver::Lifetime ResolverService::getLifetime() const
{
    return lifetime;
}

// DFS::titleMessage ResolverService::getTitle() const
//{
//    return title;
//}

ResolverService::ResolverService(Resolver::Type type, Lifetime lifetime, ActorIndex *actorIndex,
                                 ResolveManager *resolveManager, QObject *parent)
    : QObject(parent)
{
    this->type = type;
    this->lifetime = lifetime;
    this->actorIndex = actorIndex;
    this->resolveManager = resolveManager;
}

ResolverService::~ResolverService()
{
    //    emit finished();
}

void ResolverService::finishWork()
{
    active = false;

    if (this->lifetime == Resolver::Lifetime::SHORT)
    {
        emit TaskFinished();
    }
}

bool ResolverService::isActive() const
{
    return active;
}

void ResolverService::setTask(QByteArray msg, SocketPair receiver)
{
    active = true;
    this->msg = msg;
    this->hash = calcHash(msg);
    this->receiver = receiver;
}

bool ResolverService::validate(const Messages::BaseMessage &message)
{
    BigNumber signer = message.signer;
    if (signer.toByteArray().size() != 20 && signer.toByteArray().size() != 19)
        return false;
    Actor<KeyPublic> actor = actorIndex->getActor(signer);

    if (!actor.isEmpty())
    {
        return message.verifyDigSig(actor);
    }
    else
    {
        qDebug() << QString("There no actor[%1] locally").arg(QString(signer.toActorId()));
        //        emit SendGetActor(signer);
        //        return false;
        this->thread()->sleep(5);
        return validate(message);
    }
}

QByteArray ResolverService::calcHash(const QByteArray &request) const
{
    return Utils::calcKeccak(request);
}

bool ResolverService::MessageIsNotValid(const Messages::BaseMessage &message)
{
    if (validate(message))
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "checkMsgType(): valid";
        return false;
    }
    qWarning() << QString("Message [%1] digital sign is not valid. Signer was [%2]")
                      .arg(QString::fromLocal8Bit(message.serialize()), QString(message.signer.toActorId()));
    return true;
}

bool ResolverService::addResponseHandler(const QByteArray &message, const unsigned int &msgType)
{
    bool flag = false;
    handlerFileMutex.lock();
    QByteArray hash = Utils::calcKeccak(message);
    if (Messages::isGeneralResponse(msgType))
    {
        if (resolveManager->getRequestResponseMap()->find(hash)
            == resolveManager->getRequestResponseMap()->end())
        {
            resolveManager->getRequestResponseMap()->insert(hash, Config::Net::NECESSARY_RESPONSE_COUNT);
            flag = true;
        }
    }
    handlerFileMutex.unlock();
    return flag;
}

bool ResolverService::checkResponseHandler(const QByteArray &hash)
{
    handlerFileMutex.lock();
    bool flag = true;
    int value = Config::Net::NECESSARY_RESPONSE_COUNT;
    QMap<QByteArray, int>::iterator it = resolveManager->getRequestResponseMap()->find(hash);
    if (it != resolveManager->getRequestResponseMap()->end())
    {
        int t = it.value() - 1;
        if (t <= 0)
        {

            //            requestResponseMap->remove(hash);
            flag = false;
        }
        else
        {
            resolveManager->getRequestResponseMap()->remove(hash);
            resolveManager->getRequestResponseMap()->insert(hash, t);
        }
    }
    else
    {
        resolveManager->getRequestResponseMap()->insert(hash, value);
    }

    handlerFileMutex.unlock();
    return flag;
}

void ResolverService::process()
{
    resolveTask();
}

void ResolverService::resolveTask()
{
    switch (this->type)
    {
    case Resolver::Type::GENERAL:
        resolveGeneralTask();
        break;
    default:
        break;
    }
}

void ResolverService::resolveGeneralTask()
{
    QList<QByteArray> res = Serialization::universalDeserialize(msg);
    using namespace Messages;
    BaseMessage message;
    message = msg;

    unsigned int msgType = message.type;
    if (msgType == 0)
    {
        qDebug() << "msgType.isEmpty()";
    }
    if (message.data.isEmpty() && msgType != Messages::GeneralRequest::GetAllActors
        && msgType != Messages::GeneralRequest::GetBlockCount)
    {
        finishWork();
        return;
    }
    //    if (msgType != Messages::GeneralRequest::getAllActors
    //        && msgType != Messages::GeneralResponse::getAllActorsResponse)
    qDebug() << "Resolver: receive " << msgType;
    if ((msgType != Messages::ChainMessage::actorMessage) && (Messages::isDFSMessage(msgType))
        && (msgType != Messages::GeneralRequest::GetActor)
        && (msgType != Messages::GeneralResponse::getActorResponse)
        && (msgType != Messages::GeneralRequest::GetAllActors)
        && (msgType != Messages::GeneralResponse::getAllActorsResponse))
    {
        if (Messages::isGeneralResponse(msgType))
        {
            BaseMessageResponse responseMessage;
            responseMessage = msg;
            if (MessageIsNotValid(responseMessage))
            {
                finishWork();
                return;
            }
        }
        else
        {
            //            qDebug() << "received msg signature:" << message.getDigSig();
            if (MessageIsNotValid(message))
            {
                finishWork();
                return;
            }
        }
    }
    switch (msgType)
    {
    case Messages::GeneralRequest::GetAllActors: {
        actorIndex->handleGetAllActor(calcHash(msg), receiver);
        finishWork();
        break;
    }
    case Messages::GeneralResponse::getAllActorsResponse: {
        BaseMessageResponse responseMessage;
        responseMessage = msg;
        if (checkResponseHandler(responseMessage.dataHash))
            return;
        actorIndex->handleNewAllActors(Serialization::universalDeserialize(responseMessage.data, 4));
        finishWork();
        break;
    }
        // spread messages
    case Messages::ChainMessage::profileMessage: {
        emit newProfile(message.data);
        finishWork();
        break;
    }
    case Messages::ChainMessage::actorMessage: {
        Actor<KeyPublic> actor(message.data);
        actorIndex->handleNewActor(actor);
        //        emit newActor(actor);
        finishWork();
        break;
    }
    case Messages::ChainMessage::blockMessage: {
        Block block(message.data);
        if (!validateBlock(block))
        {
            qDebug() << "Received block" << block.getIndex() << "is not valid";
            finishWork();
            return;
        }
        blockchain->addBlockToBlockchain(block);
        //        emit newBlock(block);
        finishWork();
        break;
    }
    case Messages::ChainMessage::genesisBlockMessage: {
        GenesisBlock block = message.data;
        blockchain->addGenBlockToBlockchain(block);
        //        emit newGenesisBlock(block);
        finishWork();
        break;
    }
    case Messages::ChainMessage::coinRequest: {
        QByteArray msg = message.data;
        auto list = msg.split(' ');
        BigNumber amount(list[0]);
        BigNumber plsr;
        if (list.length() > 1)
            plsr = BigNumber(list[1]);
        node->coinResponse(message.signer, amount, plsr);
        //        emit coinRequest(message.getSigner(), amount);
        finishWork();
        break;
    }
    case Messages::ChainMessage::txMessage: {
        Transaction tx(message.data);
        //        if (!validate(tx))
        //        {
        //            qDebug() << "Received tx" << tx.getHash() << "is not valid";
        //            return;
        //        }
        emit newTx(tx);
        finishWork();
        break;
    }
    case Messages::ChainMessage::contractMessage: {
        //        Contract contract(message.getMsg_data());
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: "
                 << "CONTRACT_MESSAGE";
        Transaction tx(message.data);
        if (!validate(tx))
        {
            qDebug() << "Received tx of contract" << tx.getHash() << "is not valid";
            return;
        }
        emit newTx(tx);
        finishWork();
        break;
    }
    // request messages
    case Messages::GeneralRequest::GetActor: {
        GetActorMessage response;
        response = message.data;
        actorIndex->handleGetActor(response.actorId, calcHash(msg), receiver);
        finishWork();
        break;
    }
    case Messages::GeneralRequest::GetTx: {
        GetTxMessage txMessage;
        txMessage = message.data;
        emit getTx(txMessage.param, txMessage.value, receiver, calcHash(msg));
        finishWork();
        break;
    }
    case Messages::GeneralRequest::GetBlock: {
        GetBlockMessage blMessage;
        blMessage = message.data;
        emit getBlock(blMessage.param, blMessage.value, calcHash(msg), receiver);
        finishWork();
        break;
    }
    case Messages::GeneralRequest::GetActorCount: {
        emit getActorsCount(calcHash(msg), receiver);
        finishWork();
        break;
    }
    case Messages::GeneralRequest::GetBlockCount: {
        emit getBlocksCount(calcHash(msg), receiver);
        finishWork();
        break;
    }

    // response messages
    case Messages::GeneralResponse::getActorResponse: {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: "
                 << "GET_ACTOR_RESPONSE_MESSAGE"
                 << "\nmessage: " << msg;
        BaseMessageResponse responseMessage;
        responseMessage = msg;
        if (checkResponseHandler(responseMessage.dataHash))
            return;
        actorIndex->handleNewActor(Actor<KeyPublic>(responseMessage.data));
        finishWork();
        break;
    }

    case Messages::GeneralResponse::getTxResponse: {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: "
                 << "GET_TX_RESPONSE_MESSAGE";
        BaseMessageResponse responseMessage;
        responseMessage = msg;
        if (checkResponseHandler(responseMessage.dataHash))
            return;
        Transaction tx(responseMessage.data);
        if (!validate(tx))
        {
            qDebug() << "Received tx" << tx.getHash() << "is not valid";
            return;
        }
        emit newTx(tx);
        finishWork();
        break;
    }
    case Messages::GeneralResponse::getBlockResponse: {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: "
                 << "GET_BLOCK_RESPONSE_MESSAGE";

        BaseMessageResponse responseMessage;
        responseMessage = msg;
        if (checkResponseHandler(responseMessage.dataHash))
            return;
        if (GenesisBlock::isGenesisBlock(msg))
        {
            GenesisBlock gblock(responseMessage.data);
            if (!validateBlock(gblock))
            {
                qDebug() << "Received block" << gblock.getIndex() << "is not valid";
                return;
            }
            emit newGenesisBlock(gblock);
        }
        else
        {
            Block block(responseMessage.data);
            if (!validateBlock(block))
            {
                qDebug() << "Received block" << block.getIndex() << "is not valid";
                return;
            }
            blockchain->addBlockToBlockchain(block);
            //            emit newBlock(block);
        }
        finishWork();
        break;
    }
    case Messages::GeneralResponse::getBlockCountResponse: {
        BaseMessageResponse responseMessage;
        responseMessage = msg;
        if (checkResponseHandler(responseMessage.dataHash))
            return;
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: "
                 << "GET_BLOCK_COUNT_RESPONSE_MESSAGE";
        BigNumber count(responseMessage.data);
        emit blockCount(count);
        finishWork();
        break;
    }
    case Messages::GeneralResponse::getActorCountResponse: {
        BaseMessageResponse responseMessage;
        responseMessage = msg;
        if (checkResponseHandler(responseMessage.dataHash))
            return;
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: "
                 << "GET_ACTOR_COUNT_RESPONSE_MESSAGE";
        finishWork();
        break;
    }
    default: {
        finishWork();
        break;
    }
    }
}
// validation methods //

bool ResolverService::validateBlock(const Block &block)
{
    qDebug() << "RESOLVER SERVICE: "
             << "validate(Block):";
    return actorIndex->validateBlock(block);
}

bool ResolverService::validate(const Transaction &tx)
{
    qDebug() << "RESOLVER SERVICE: "
             << "validate(Transaction):";
    bool result = actorIndex->validateTx(tx);
    //    if (tx.getData() == "initcontract")
    //        result = (result && !actorIndex->getActor(tx.getSender()).profile().getProfile().isEmpty());
    if (!actorIndex->getActor(tx.getSender()).isEmpty())
        if (actorIndex->getActor(tx.getSender()).profile().getProfile().isEmpty())
        {
            this->thread()->sleep(5);
            return validate(tx);
        }
    return result;
}
