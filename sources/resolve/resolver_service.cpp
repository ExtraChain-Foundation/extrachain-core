#include "headers/resolve/resolver_service.h"

ResolverService::ResolverService(QMap<QByteArray, int> *rrMap, QObject *parent)
    : QObject(parent)
{
    requestResponseMap = rrMap;
}

ResolverService::ResolverService(ActorIndex *actorIndex, QMap<QByteArray, int> *rrMap, QObject *parent)
    : QObject(parent)
{
    this->actorIndex = actorIndex;
    requestResponseMap = rrMap;
}

ResolverService::~ResolverService()
{
    emit finished();
}

bool ResolverService::isActive() const
{
    return active;
}

void ResolverService::setTask(QByteArray msg, SocketPair receiver)
{
    this->msg = msg;
    this->hash = calcHash(msg);
    this->senderAddress = receiver;
}

bool ResolverService::validate(const Messages::IMessage &message)
{
    BigNumber signer = message.getSigner();

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

QByteArray ResolverService::checkMsgType(const QByteArray &msg) const
{

    Messages::BaseMessage b;
    b.deserialize(msg);
    return b.getMsgType();
}

QByteArray ResolverService::calcHash(const QByteArray &request) const
{
    qDebug() << "RESOLVER SERVICE: "
             << "calcHash()";
    return Utils::calcKeccak(request);
}

bool ResolverService::MessageIsNotValid(const Messages::IMessage &message)
{
    // qDebug() << "RESOLVER SERVICE: " << "MessageIsNotValid(): ";

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
        if (requestResponseMap->find(hash) == requestResponseMap->end())
        {
            requestResponseMap->insert(hash, Config::Net::NECESSARY_RESPONSE_COUNT);
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
    QMap<QByteArray, int>::iterator it = requestResponseMap->find(hash);
    if (it != requestResponseMap->end())
    {
        int t = it.value() - 1;
        if (t <= 0)
        {

            //            requestResponseMap->remove(hash);
            flag = false;
        }
        else
        {
            requestResponseMap->remove(hash);
            requestResponseMap->insert(hash, t);
        }
    }
    else
    {
        requestResponseMap->insert(hash, value);
    }

    handlerFileMutex.unlock();
    return flag;
}

void ResolverService::process()
{
    recieveMsg(this->msg, this->senderAddress);
}

void ResolverService::recieveMsg(const QByteArray &msg, const SocketPair &receiver)
{
    using namespace Messages;
    BaseMessage message;
    message.deserialize(msg);
    QByteArray msgType = message.getMsgType();
    qDebug() << "Resolver: receive " << msgType;
    if ((msgType != ACTOR_MESSAGE) && (msgType != DFS_CHANGES_MESSAGE)
        && (msgType != GET_ACTOR_RESPONSE_MESSAGE))
        if (MessageIsNotValid(message))
            return;
    // spread messages
    if (msgType == PROFILE_FILE)
    {
        emit newProfile(message.getMsg_data());
        emit TaskFinished();
    }
    else if (msgType == ACTOR_MESSAGE)
    {
        Actor<KeyPublic> actor(message.getMsg_data());
        emit newActor(actor);
        emit TaskFinished();
    }
    else if (msgType == DFS_CHANGES_MESSAGE)
    {
        DfsMessage message(msg);
        emit newDfsPack(message);
        emit TaskFinished();
    }
    else if (msgType == BLOCK_MESSAGE)
    {
        Block block(message.getMsg_data());
        if (!validate(block))
        {
            qDebug() << "Received block" << block.getIndex() << "is not valid";
            return;
        }
        emit newBlock(block);
        emit TaskFinished();
    }
    else if (msgType == GENESIS_BLOCK_MESSAGE)
    {
        Block block = message.getMsg_data();
        emit newBlock(block);
        emit TaskFinished();
    }
    else if (msgType == COIN_REQUEST)
    {
        BigNumber amount(message.getMsg_data());
        emit coinRequest(message.getSigner(), amount);
        emit TaskFinished();
    }
    else if (msgType == TX_MESSAGE)
    {
        Transaction tx(message.getMsg_data());
        if (!validate(tx))
        {
            qDebug() << "Received tx" << tx.getHash() << "is not valid";
            return;
        }
        emit newTx(tx);
        emit TaskFinished();
    }
    else if (msgType == CONTRACT_MESSAGE)
    {
        //        Contract contract(message.getMsg_data());
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << CONTRACT_MESSAGE;
        emit TaskFinished();
    }

    else if (msgType == MERGED_BLOCK_MESSAGE)
    {
        //
        qDebug() << "[resolve message] MERGED_BLOCK_MESSAGE";
        emit TaskFinished();
    }
    else if (msgType == BLOCK_APPROVED_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << BLOCK_APPROVED_MESSAGE;
        BlockApprovedMessage r(message.getMsg_data());
        emit TaskFinished();

        //        emit BlockApproved(message.getBlockId(), message.getApprover(), peerAddress);
    }

    // request messages
    else if (msgType == GET_ACTOR_MESSAGE)
    {
        GetActorMessage response(message.getMsg_data());
        emit getActor(response.getActorId(), calcHash(msg), receiver);
    }
    else if (msgType == GET_TX_MESSAGE)
    {
        GetTxMessage txMessage(message.getMsg_data());
        emit getTx(txMessage.getParam(), txMessage.getValue(), receiver, calcHash(msg));
    }
    else if (msgType == GET_BLOCK_MESSAGE)
    {
        GetBlockMessage blMessage(message.getMsg_data());
        emit getBlock(blMessage.getParam(), blMessage.getValue(), calcHash(msg), receiver);
    }
    else if (msgType == GET_ACTOR_COUNT_MESSAGE)
    {
        emit getActorsCount(calcHash(msg), receiver);
    }
    else if (msgType == GET_BLOCK_COUNT_MESSAGE)
    {
        emit getBlocksCount(calcHash(msg), receiver);
    }

    // response messages
    else if (msgType == GET_ACTOR_RESPONSE_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_ACTOR_RESPONSE_MESSAGE << "\nmessage: " << msg;
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            emit newActor(Actor<KeyPublic>(responseMessage.getMsg_data()));
        emit TaskFinished();
    }
    else if (msgType == GET_TX_RESPONSE_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_TX_RESPONSE_MESSAGE;
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        Transaction tx(responseMessage.getMsg_data());
        if (!validate(tx))
        {
            qDebug() << "Received tx" << tx.getHash() << "is not valid";
            return;
        }
        emit newTx(tx);
        emit TaskFinished();
    }
    else if (msgType == GET_BLOCK_RESPONSE_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_BLOCK_RESPONSE_MESSAGE;

        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        Block block(responseMessage.getMsg_data());
        if (!validate(block))
        {
            qDebug() << "Received block" << block.getIndex() << "is not valid";
            return;
        }
        emit newBlock(block);
        emit TaskFinished();
    }
    else if (msgType == GET_BLOCK_COUNT_RESPONSE_MESSAGE)
    {
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_BLOCK_COUNT_RESPONSE_MESSAGE;
        BigNumber count(responseMessage.getMsg_data());
        emit blockCount(count);
        emit TaskFinished();
    }
    else if (msgType == GET_ACTOR_COUNT_RESPONSE_MESSAGE)
    {
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_ACTOR_COUNT_RESPONSE_MESSAGE;
        emit TaskFinished();
    }
}

// validation methods //

bool ResolverService::validate(const Block &block)
{
    qDebug() << "RESOLVER SERVICE: "
             << "validate(Block):";
    return actorIndex->validateBlock(block);
}

bool ResolverService::validate(const Transaction &tx)
{
    qDebug() << "RESOLVER SERVICE: "
             << "validate(Transaction):";
    return actorIndex->validateTx(tx);
}
