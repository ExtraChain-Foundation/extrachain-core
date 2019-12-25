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

DFSMessage::title_message ResolverService::getTitle() const
{
    return title;
}

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

bool ResolverService::validate(const Messages::IMessage &message)
{
    BigNumber signer = message.getSigner();
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

bool ResolverService::MessageIsNotValid(const Messages::IMessage &message)
{
    if (validate(message))
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "checkMsgType(): valid";
        return false;
    }
    qWarning() << QString("Message [%1] digital sign is not valid. Signer was [%2]")
                      .arg(QString::fromLocal8Bit(message.serialize()),
                           QString(message.getSigner().toActorId()));
    return true;
}

bool ResolverService::addResponseHandler(const QByteArray &message, const QByteArray &msgType)
{
    bool flag = false;
    handlerFileMutex.lock();
    QByteArray hash = Utils::calcKeccak(message);
    if (Messages::RESPONSE.contains(msgType))
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
    message.deserialize(msg);

    QByteArray msgType = message.getMsgType();
    QByteArray data_test = message.serialize();
    if (msgType.isEmpty())
    {
        qDebug() << "msgType.isEmpty()";
    }
    if (message.getData().isEmpty() && msgType != GET_ALL_ACTORS && msgType != GET_BLOCK_COUNT_MESSAGE)
    {
        finishWork();
        return;
    }
    if (msgType != GET_ALL_ACTORS && msgType != GET_ALL_ACTORS_RESPONSE_MESSAGE)
        qDebug() << "Resolver: receive " << msgType;
    if ((msgType != ACTOR_MESSAGE) && (msgType != DFS_MESSAGE) && (msgType != GET_ACTOR_RESPONSE_MESSAGE)
        && (msgType != GET_ACTOR_MESSAGE) && (msgType != GET_ALL_ACTORS)
        && (msgType != GET_ALL_ACTORS_RESPONSE_MESSAGE))
    {
        if (RESPONSE.contains(msgType))
        {
            BaseMessageResponse responseMessage(msg);
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
    if (msgType == GET_ALL_ACTORS)
    {
        //        GetAllActorMessage response(message.getMsg_data());
        emit handleGetAllActor(calcHash(msg), receiver);
        finishWork();
    }
    else if (msgType == GET_ALL_ACTORS_RESPONSE_MESSAGE)
    {
        //        qDebug() << "RESOLVER SERVICE: "
        //                 << "recieveMsg(): type: " << GET_ALL_ACTORS_RESPONSE_MESSAGE << "\nmessage: " <<
        //                 msg;
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        actorIndex->handleNewAllActors(Serialization::universalDeserialize(responseMessage.getData(), 4));
        //        emit newActor(Actor<KeyPublic>(responseMessage.getMsg_data()));
        finishWork();
    }
    else if ((msgType == INVITE_CHAT_MESSAGE) || (msgType == CHAT_MESSAGE))
    {
        //
        chatManager->msgReceiver(message);
        finishWork();
    }
    // spread messages
    else if (msgType == PROFILE_FILE)
    {
        emit newProfile(message.getData());
        finishWork();
    }
    else if (msgType == ACTOR_MESSAGE)
    {
        Actor<KeyPublic> actor(message.getData());
        actorIndex->handleNewActor(actor);
        //        emit newActor(actor);
        finishWork();
    }
    else if (msgType == BLOCK_MESSAGE)
    {
        Block block(message.getData());
        if (!validateBlock(block))
        {
            qDebug() << "Received block" << block.getIndex() << "is not valid";
            finishWork();
            return;
        }
        blockchain->addBlockToBlockchain(block);
        //        emit newBlock(block);
        finishWork();
    }
    else if (msgType == GENESIS_BLOCK_MESSAGE)
    {
        GenesisBlock block = message.getData();
        blockchain->addGenBlockToBlockchain(block);
        //        emit newGenesisBlock(block);
        finishWork();
    }
    else if (msgType == COIN_REQUEST)
    {
        QByteArray msg = message.getData();
        auto list = msg.split(' ');
        BigNumber amount(list[0]);
        BigNumber plsr;
        if (list.length() > 1)
            plsr = BigNumber(list[1]);
        node->coinResponse(message.getSigner(), amount, plsr);
        //        emit coinRequest(message.getSigner(), amount);
        finishWork();
    }
    else if (msgType == TX_MESSAGE)
    {
        Transaction tx(message.getData());
        //        if (!validate(tx))
        //        {
        //            qDebug() << "Received tx" << tx.getHash() << "is not valid";
        //            return;
        //        }
        emit newTx(tx);
        finishWork();
    }
    else if (msgType == CONTRACT_MESSAGE)
    {
        //        Contract contract(message.getMsg_data());
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << CONTRACT_MESSAGE;
        Transaction tx(message.getData());
        if (!validate(tx))
        {
            qDebug() << "Received tx of contract" << tx.getHash() << "is not valid";
            return;
        }
        emit newTx(tx);
        finishWork();
    }

    else if (msgType == MERGED_BLOCK_MESSAGE)
    {
        //
        qDebug() << "[resolve message] MERGED_BLOCK_MESSAGE";
        finishWork();
    }
    else if (msgType == BLOCK_APPROVED_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << BLOCK_APPROVED_MESSAGE;
        BlockApprovedMessage r(message.getData());
        finishWork();

        //        emit BlockApproved(message.getBlockId(), message.getApprover(), peerAddress);
    }
    // request messages
    else if (msgType == GET_ACTOR_MESSAGE)
    {
        GetActorMessage response(message.getData());
        emit getActor(response.getActorId(), calcHash(msg), receiver);
        finishWork();
    }

    else if (msgType == GET_TX_MESSAGE)
    {
        GetTxMessage txMessage(message.getData());
        emit getTx(txMessage.getParam(), txMessage.getValue(), receiver, calcHash(msg));
        finishWork();
    }
    else if (msgType == GET_BLOCK_MESSAGE)
    {
        GetBlockMessage blMessage(message.getData());
        emit getBlock(blMessage.getParam(), blMessage.getValue(), calcHash(msg), receiver);
        finishWork();
    }
    else if (msgType == GET_ACTOR_COUNT_MESSAGE)
    {
        emit getActorsCount(calcHash(msg), receiver);
        finishWork();
    }
    else if (msgType == GET_BLOCK_COUNT_MESSAGE)
    {
        emit getBlocksCount(calcHash(msg), receiver);
        finishWork();
    }

    // response messages
    else if (msgType == GET_ACTOR_RESPONSE_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_ACTOR_RESPONSE_MESSAGE << "\nmessage: " << msg;
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        actorIndex->handleNewActor(Actor<KeyPublic>(responseMessage.getData()));
        //        emit newActor(Actor<KeyPublic>(responseMessage.getMsg_data()));
        finishWork();
    }

    else if (msgType == GET_TX_RESPONSE_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_TX_RESPONSE_MESSAGE;
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        Transaction tx(responseMessage.getData());
        if (!validate(tx))
        {
            qDebug() << "Received tx" << tx.getHash() << "is not valid";
            return;
        }
        emit newTx(tx);
        finishWork();
    }
    else if (msgType == GET_BLOCK_RESPONSE_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_BLOCK_RESPONSE_MESSAGE;

        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        if (GenesisBlock::isGenesisBlock(msg))
        {
            GenesisBlock gblock(responseMessage.getData());
            if (!validateBlock(gblock))
            {
                qDebug() << "Received block" << gblock.getIndex() << "is not valid";
                return;
            }
            emit newGenesisBlock(gblock);
        }
        else
        {
            Block block(responseMessage.getData());
            if (!validateBlock(block))
            {
                qDebug() << "Received block" << block.getIndex() << "is not valid";
                return;
            }
            blockchain->addBlockToBlockchain(block);
            //            emit newBlock(block);
        }
        finishWork();
    }
    else if (msgType == GET_BLOCK_COUNT_RESPONSE_MESSAGE)
    {
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_BLOCK_COUNT_RESPONSE_MESSAGE;
        BigNumber count(responseMessage.getData());
        emit blockCount(count);
        finishWork();
    }
    else if (msgType == GET_ACTOR_COUNT_RESPONSE_MESSAGE)
    {
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_ACTOR_COUNT_RESPONSE_MESSAGE;
        finishWork();
    }
    else
        finishWork();
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
