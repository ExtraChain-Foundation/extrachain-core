#include "headers/resolve/resolver_service.h"

ResolverService::ResolverService(QMap<QByteArray, int> *rrMap, QObject *parent)
    : QObject(parent)
{
    requestResponseMap = rrMap;
    //    actorIndex = new ActorIndex;
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
    this->hash = hash;
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
        qDebug() << QString("There no actor[%1] locally").arg(QString(signer.toByteArray()));
        //        emit SendGetActor(signer);
        return false;
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
                           QString(message.getSigner().toByteArray()));
    return true;
}

bool ResolverService::universalHandler(const Messages::IMessage &msg)
{
    // verify
    if (checkMsgCount(msg))
    {
        emit secondWave(msg.serialize());
        return true;
    }
    else
        return false;
}

bool ResolverService::addResponseHandler(const QByteArray &message, const QByteArray &msgType)
{
    QByteArray hash = Utils::calcKeccak(message);
    if (Messages::RESPONSE.contains(msgType))
    {
        if (requestResponseMap->find(hash) == requestResponseMap->end())
        {
            handlerFileMutex.lock();
            requestResponseMap->insert(hash, Config::Net::NECESSARY_RESPONSE_COUNT);
            handlerFileMutex.unlock();
            return true;
        }
        else
            return false;
    }
    else
    {
        return false;
    }
}

bool ResolverService::checkResponseHandler(const QByteArray &hash)
{
    QMap<QByteArray, int>::iterator it = requestResponseMap->find(hash);
    if (it != requestResponseMap->end())
    {
        int t = it.value() - 1;
        if (t <= 0)
        {
            handlerFileMutex.lock();
            requestResponseMap->remove(hash);
            handlerFileMutex.unlock();
            return true;
        }
        else
        {
            handlerFileMutex.lock();
            requestResponseMap->remove(hash);
            requestResponseMap->insert(hash, t);
            handlerFileMutex.unlock();
            return false;
        }
    }
    else
    {
        return false;
    }
}

bool ResolverService::checkMsgCount(const Messages::IMessage &msg)
{
    handlerFileMutex.lock();
    bool flag_result = true;
    short value = 0;
    QFile file(".handler");
    FileList handlerList;
    handlerList.setFileList(file);
    if (handlerList.at(msg.hash()) == "")
        handlerList.add(msg.hash(), QByteArray::number(value));
    else
    {
        short msg_count = handlerList.at(msg.hash()).toShort();
        msg_count--;
        if (msg_count == -1)
        {
            flag_result = false;
            handlerList.remove(msg.hash());
        }
        else
        {
            short amount = handlerList.at(msg.hash()).toShort();
            amount--;
            handlerList.remove(msg.hash());
            handlerList.add(msg.hash(), QByteArray::number(amount));
        }
    }
    handlerFileMutex.unlock();
    return flag_result;
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
    if (msgType != ACTOR_MESSAGE)
        if (!MessageIsNotValid(message))
            return;
    universalHandler(message);
    // spread messages
    if (msgType == PROFILE_FILE)
    {
        emit newProfile(message.getMsg_data());
    }
    else if (msgType == ACTOR_MESSAGE)
    {
        Actor<KeyPublic> actor(message.getMsg_data());
        emit newActor(actor);
    }
    else if (msgType == DFS_CHANGES_MESSAGE)
    {
        DfsMessage message(msg);
        emit newDfsPack(message);
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
    }
    else if (msgType == GENESIS_BLOCK_MESSAGE)
    {
        Block block = message.getMsg_data();
        emit newBlock(block);
    }
    else if (msgType == COIN_REQUEST)
    {
        BigNumber amout(message.getMsg_data());
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
    }
    else if (msgType == CONTRACT_MESSAGE)
    {
        Contract contract(message.getMsg_data());

        emit contractFromNetwork(contract);
    }

    else if (msgType == MERGED_BLOCK_MESSAGE)
    {
        //
        qDebug() << "[resolve message] MERGED_BLOCK_MESSAGE";
    }
    else if (msgType == BLOCK_APPROVED_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << BLOCK_APPROVED_MESSAGE;
        BlockApprovedMessage message(msg);
        if (MessageIsNotValid(message))
            return;

        //        emit BlockApproved(message.getBlockId(), message.getApprover(), peerAddress);
    }

    // request messages
    else if (msgType == GET_ACTOR_MESSAGE)
    {
        GetActorMessage message(msg);
        //        if (addResponseHandler(msg, msgType))
        emit getActor(message.getActorId(), calcHash(msg), receiver);
    }
    /**
    else if (msgType == GET_TX_MESSAGE)
    {
        GetTxMessage message(msg);
        emit getTx(message.getParam(), message.getValue(), "peerAddress");
    }
    else if (msgType == GET_TX_PAIR_MESSAGE)
    {
        GetTxPairMessage message(msg);
        emit getTxPair(message.getSenderId(), message.getReceiverId(), peerAddress);
    }

    else if (msgType == GET_BLOCK_MESSAGE)
    {
        GetBlockMessage message(msg);
        emit getBlock(message.getParam(), message.getValue(), peerAddress);
    }
    else if (msgType == GET_ACTOR_MESSAGE)
    {
        GetActorMessage message(msg);
        emit getActor(message.getActorId(), "", ); // TODO : peer adress
    }
    else if (msgType == GET_ACTOR_COUNT_MESSAGE)
    {
        BaseMessage message = BaseMessage::deserializeMsg(msg);
        emit getActorsCount(peerAddress);
    }
    else if (msgType == GET_BLOCK_COUNT_MESSAGE)
    {
        BaseMessage message = BaseMessage::deserializeMsg(msg);
        emit getBlocksCount(peerAddress);
    }
    **/
    // response messages
    else if (msgType == GET_ACTOR_RESPONSE_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_ACTOR_RESPONSE_MESSAGE << "\nmessage: " << msg;
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            emit newActor(Actor<KeyPublic>(responseMessage.getMsg_data()));
    }
    //    else if (msgType == GET_TX_RESPONSE_MESSAGE)
    //    {
    //        qDebug() << "RESOLVER SERVICE: "
    //                 << "recieveMsg(): type: " << GET_TX_RESPONSE_MESSAGE;
    //        EntityResponseMessage<Transaction> message(msg);
    //        if (MessageIsNotValid(message))
    //            return;

    //        Transaction tx = message.getEntity();
    //        if (!validate(tx))
    //        {
    //            qDebug() << "Received tx" << tx.getHash() << "is not valid";
    //            return;
    //        }

    //        //        emit GetTxResponse(tx, calcHash(msg), peerAddress);
    //    }
    //    else if (msgType == GET_TX_PAIR_RESPONSE_MESSAGE)
    //    {
    //        qDebug() << "RESOLVER SERVICE: "
    //                 << "recieveMsg(): type: " << GET_TX_PAIR_RESPONSE_MESSAGE;
    //        EntityResponseMessage<TxPair> message(msg);
    //        if (MessageIsNotValid(message))
    //            return;

    //        TxPair pair = message.getEntity();
    //        if (!validate(pair.getFirst()) || !validate(pair.getSecond()))
    //        {
    //            qDebug() << QString("In Received message [%1] At least one tx is not valid")
    //                            .arg(QString::fromLocal8Bit(message.serialize()));
    //            return;
    //        }

    //        //        emit GetTxPairResponse(pair, calcHash(msg), peerAddress);
    //    }
    //    else if (msgType == GET_BLOCK_RESPONSE_MESSAGE)
    //    {
    //        qDebug() << "RESOLVER SERVICE: "
    //                 << "recieveMsg(): type: " << GET_BLOCK_RESPONSE_MESSAGE;
    //        EntityResponseMessage<Block> message(msg);
    //        if (MessageIsNotValid(message))
    //            return;

    //        Block block = message.getEntity();
    //        if (!validate(block))
    //        {
    //            qDebug() << "Received block" << block.getIndex() << "is not valid";
    //            return;
    //        }

    //        //        emit GetBlockResponse(message.getEntity(), message.getRequestHash(), peerAddress);
    //    }
    //    else if (msgType == GET_ACTOR_RESPONSE_MESSAGE)
    //    {
    //        qDebug() << "RESOLVER SERVICE: "
    //                 << "recieveMsg(): type: " << GET_ACTOR_RESPONSE_MESSAGE << "\nmessage: " << msg;
    //        EntityResponseMessage<Actor<KeyPublic>> message(msg);
    //        //        if (MessageIsNotValid(message))
    //        //            return;
    //        //        emit GetActorResponse(message.getEntity(), message.getRequestHash(), peerAddress);
    //    }

    //    else if (msgType == GET_ACTOR_COUNT_RESPONSE_MESSAGE)
    //    {
    //        qDebug() << "RESOLVER SERVICE: "
    //                 << "recieveMsg(): type: " << GET_ACTOR_COUNT_RESPONSE_MESSAGE;
    //        EntityResponseMessage<BigNumber> message(msg);
    //        //        if (MessageIsNotValid(message))
    //        //            return;
    //        //        emit GetActorCountResponse(message.getEntity(), message.getRequestHash(),
    //        peerAddress);
    //    }
    //    else if (msgType == GET_BLOCK_COUNT_RESPONSE_MESSAGE)
    //    {
    //        qDebug() << "RESOLVER SERVICE: "
    //                 << "recieveMsg(): type: " << GET_BLOCK_COUNT_RESPONSE_MESSAGE;
    //        EntityResponseMessage<BigNumber> message(msg);
    //        //        if (MessageIsNotValid(message))
    //        //            return;

    //        //        emit GetBlockCountResponse(message.getEntity(), message.getRequestHash(),
    //        peerAddress);
    //    }
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
