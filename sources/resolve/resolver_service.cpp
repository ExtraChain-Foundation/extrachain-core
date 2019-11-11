#include "headers/resolve/resolver_service.h"

void ResolverService::setNode(NodeManager *value)
{
    node = value;
}

void ResolverService::setBlockchain(Blockchain *value)
{
    blockchain = value;
}

void ResolverService::setDfs(Dfs *value)
{
    dfs = value;
}

ResolverService::ResolverService(ActorIndex *actorIndex, QMap<QByteArray, int> *rrMap,
                                 QMap<QByteArray, QFile *> *listFile, QMap<QString, QByteArray> *fileMap,
                                 QMap<QByteArray, long long> *pckgCounter, QObject *parent)
    : QObject(parent)
{
    this->actorIndex = actorIndex;
    requestResponseMap = rrMap;
    this->listFile = listFile;
    this->fileMap = fileMap;
    this->pckgCounter = pckgCounter;
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

// QByteArray ResolverService::checkMsgType(const QByteArray &msg) const
//{

//    Messages::BaseMessage b;
//    b.deserialize(msg);
//    return b.getMsgType();
//}

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
// getAllActors
// return all id actors that have current actor from actorIndex

/*

 */
void ResolverService::recieveMsg(const QByteArray &msg, const SocketPair &receiver)
{
    using namespace Messages;
    BaseMessage message;
    message.deserialize(msg);
    QByteArray msgType = message.getMsgType();
    qDebug() << "Resolver: receive " << msgType;
    if ((msgType != ACTOR_MESSAGE) && (msgType != DFS_MESSAGE) && (msgType != GET_ACTOR_RESPONSE_MESSAGE))
    {
        if (RESPONSE.contains(msgType))
        {
            BaseMessageResponse responseMessage(msg);
            if (MessageIsNotValid(responseMessage))
                return;
        }
        else
        {
            qDebug() << "received msg signature:" << message.getDigSig();
            if (MessageIsNotValid(message))
                return;
        }
    }
    // dfs message
    if (msgType == DFS_MESSAGE)
    {
        qDebug() << "[&Resolver:]" << DFS_MESSAGE << "is detected";
        Message::DUMessage dfsMsg(message.getMsg_data());
        resolveDfsMessage(message.getMsg_data(), dfsMsg.getType(), receiver);
        emit TaskFinished();
    }
    // spread messages
    else if (msgType == PROFILE_FILE)
    {
        emit newProfile(message.getMsg_data());
        emit TaskFinished();
    }
    else if (msgType == ACTOR_MESSAGE)
    {
        Actor<KeyPublic> actor(message.getMsg_data());
        actorIndex->handleNewActor(actor);
        //        emit newActor(actor);
        emit TaskFinished();
    }
    //    else if (msgType == DFS_CHANGES_MESSAGE)
    //    {
    //        DfsMessage message(msg);
    //        emit newDfsPack(message);
    //        emit TaskFinished();
    //    }
    else if (msgType == BLOCK_MESSAGE)
    {
        Block block(message.getMsg_data());
        if (!validateBlock(block))
        {
            qDebug() << "Received block" << block.getIndex() << "is not valid";
            return;
        }
        blockchain->addBlockToBlockchain(block);
        //        emit newBlock(block);
        emit TaskFinished();
    }
    else if (msgType == GENESIS_BLOCK_MESSAGE)
    {
        GenesisBlock block = message.getMsg_data();
        blockchain->addGenBlockToBlockchain(block);
        //        emit newGenesisBlock(block);
        emit TaskFinished();
    }
    else if (msgType == COIN_REQUEST)
    {
        BigNumber amount(message.getMsg_data());
        node->coinResponse(message.getSigner(), amount);
        //        emit coinRequest(message.getSigner(), amount);
        emit TaskFinished();
    }
    else if (msgType == TX_MESSAGE)
    {
        Transaction tx(message.getMsg_data());
        //        if (!validate(tx))
        //        {
        //            qDebug() << "Received tx" << tx.getHash() << "is not valid";
        //            return;
        //        }
        emit newTx(tx);
        emit TaskFinished();
    }
    else if (msgType == CONTRACT_MESSAGE)
    {
        //        Contract contract(message.getMsg_data());
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << CONTRACT_MESSAGE;
        Transaction tx(message.getMsg_data());
        if (!validate(tx))
        {
            qDebug() << "Received tx of contract" << tx.getHash() << "is not valid";
            return;
        }
        emit newTx(tx);
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
        emit TaskFinished();
    }
    else if (msgType == GET_ALL_ACTORS)
    {
        GetAllActorMessage response(message.getMsg_data());
        emit handleGetAllActor(calcHash(msg), receiver);
        emit TaskFinished();
    }
    else if (msgType == GET_TX_MESSAGE)
    {
        GetTxMessage txMessage(message.getMsg_data());
        emit getTx(txMessage.getParam(), txMessage.getValue(), receiver, calcHash(msg));
        emit TaskFinished();
    }
    else if (msgType == GET_BLOCK_MESSAGE)
    {
        GetBlockMessage blMessage(message.getMsg_data());
        emit getBlock(blMessage.getParam(), blMessage.getValue(), calcHash(msg), receiver);
        emit TaskFinished();
    }
    else if (msgType == GET_ACTOR_COUNT_MESSAGE)
    {
        emit getActorsCount(calcHash(msg), receiver);
        emit TaskFinished();
    }
    else if (msgType == GET_BLOCK_COUNT_MESSAGE)
    {
        emit getBlocksCount(calcHash(msg), receiver);
        emit TaskFinished();
    }

    // response messages
    else if (msgType == GET_ACTOR_RESPONSE_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_ACTOR_RESPONSE_MESSAGE << "\nmessage: " << msg;
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        actorIndex->handleNewActor(Actor<KeyPublic>(responseMessage.getMsg_data()));
        //        emit newActor(Actor<KeyPublic>(responseMessage.getMsg_data()));
        emit TaskFinished();
    }
    else if (msgType == GET_ALL_ACTORS_RESPONSE_MESSAGE)
    {
        qDebug() << "RESOLVER SERVICE: "
                 << "recieveMsg(): type: " << GET_ALL_ACTORS_RESPONSE_MESSAGE << "\nmessage: " << msg;
        BaseMessageResponse responseMessage(msg);
        if (checkResponseHandler(responseMessage.getDataHash()))
            return;
        actorIndex->handleNewAllActors(Serialization::universalDeserialize(responseMessage.getMsg_data(), 4));
        //        emit newActor(Actor<KeyPublic>(responseMessage.getMsg_data()));
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
        if (GenesisBlock::isGenesisBlock(msg))
        {
            GenesisBlock gblock(responseMessage.getMsg_data());
            if (!validateBlock(gblock))
            {
                qDebug() << "Received block" << gblock.getIndex() << "is not valid";
                return;
            }
            emit newGenesisBlock(gblock);
        }
        else
        {
            Block block(responseMessage.getMsg_data());
            if (!validateBlock(block))
            {
                qDebug() << "Received block" << block.getIndex() << "is not valid";
                return;
            }
            blockchain->addBlockToBlockchain(block);
            //            emit newBlock(block);
        }
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
    else
        emit TaskFinished();
}
void ResolverService::resolveDfsMessage(const QByteArray &data, const int &mType, const SocketPair &receiver)
{
    qDebug() << "[dfs resolve message]";
    Message::dfsMessageType msgType = static_cast<Message::dfsMessageType>(mType);
    // resolve msg
    if (msgType == Message::dfsMessageType::titleMessage)
    {
        qDebug() << "[title message:]";
        Message::title_message message(data);
        QByteArray mHash = message.hash();

        if (QFile(message.filePath).exists())
            return;
        QString path = message.filePath + based_dfs_struct::FILE_IDENTIFICATOR;
        if (!registerTitle(path, message.pckgsAmount, message.fileSize, message.serialize(), mHash))
        {
            qDebug() << "Abort on register title";
            return;
        }
        qDebug() << "[file path:]" << path;
        // register title for receive
    }
    else if (msgType == Message::dfsMessageType::statusMessage)
    {
        qDebug() << "[statusMessagee:]";
        Message::Status message(data);
    }
    else if (msgType == Message::dfsMessageType::requestMessage)
    {
        qDebug() << "[requestMessage:]";
        Message::dfs_request message(data);
        dfs->fileResponce(message.filePath, receiver);
    }
    else if (msgType == Message::dfsMessageType::storageMessage)
    {
        qDebug() << "[storageMessage:]";
    }
    else if (msgType == Message::dfsMessageType::fileDataMessage)
    {
        qDebug() << "[fileDataMessage:]";
        Message::dfs_message message(data);
        QByteArray title_hash = message.title_hash;
        // setup iterator of Maps
        handlerFileMutex.lock();
        QMap<QByteArray, QFile *>::iterator listFileIT = listFile->find(title_hash);
        if (listFileIT == listFile->end())
        {
            qDebug() << "[&DFSResolver][title not found]" << message.pckgNumber;
            handlerFileMutex.unlock();
            return;
        }
        QMap<QString, QByteArray>::iterator fileMapIT = fileMap->find(listFileIT.value()->fileName());
        if (pckgCounter->find(title_hash) == pckgCounter->end())
        {
            qDebug() << "[&DFSResolver][maybe already have finished]";
            handlerFileMutex.unlock();
            return;
        }
        if (fileMapIT == fileMap->end())
        {
            qDebug() << "[&DFSResolver][file not found]";
            handlerFileMutex.unlock();
            return;
        }

        listFileIT.value()->seek(message.pckgNumber * Message::dataSize);
        if (listFileIT.value()->write(message.data))
            pckgCounter->find(title_hash).value()--;
        listFileIT.value()->flush();
        QMap<QByteArray, long long>::iterator pckgCounterIT = pckgCounter->find(title_hash);
        if (pckgCounterIT.value() == 0)
        {
            Message::title_message title(fileMapIT.value());
            qDebug() << "[&DFSResolver][file succed written to tmp]";
            if (listFileIT.value()->size() != title.fileSize)
            {
                qDebug() << "[&DFSResolver][tmp file size not enought]";
                QString path = listFileIT.value()->fileName();
                path.chop(based_dfs_struct::FILE_IDENTIFICATOR.size());
                Message::dfs_request rqst(path, ac->getCurrentActor().getId().toActorId());
                dfs->dfsNetManager->send(rqst.serialize());
                listFile->erase(listFileIT);
                listFileIT.value()->remove();
                delete listFileIT.value();
                return;
            }
            listFileIT.value()->close();
            dfs->saveFN(listFileIT.value()->fileName(), title.filePath,
                        based_dfs_struct::convertToDFType(title.f_type));
            // finish save file
            pckgCounter->erase(pckgCounterIT);
            fileMap->erase(fileMapIT);
            listFile->erase(listFileIT);
            delete listFileIT.value();
        }
        handlerFileMutex.unlock();
    }
    else if (msgType == Message::dfsMessageType::responseMessage)
    {
        qDebug() << "[responseMessage:]";
    }
    else if (msgType == Message::dfsMessageType::closingMessage)
    {
        qDebug() << "[closingMessage:]";
        Message::DClosing message(msg);
        //        emit closingMsg(message.title_hash, message.PckgAmoutR, receiver);
    }
    else
        qDebug() << "[&DFSResolver] undifine message type";
}

bool ResolverService::createTempFile(const QString &path, const long long &size, const QByteArray &tHash)
{
    qDebug() << "[&DfsResolver] start create tmp file:" << path;
    handlerFileMutex.lock();
    QFile *file = listFile->insert(tHash, new QFile(path)).value();
    if (!file->open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        // Take actorid of file owner
        QList<QByteArray> pathList = Serialization::deserialize(path.toUtf8() + '/', "/");
        qDebug() << "Create temp file: actor - " << BigNumber(pathList.at(PathStruct::aId));
        Actor<KeyPublic> actor = actorIndex->getActor(BigNumber(pathList.at(PathStruct::aId)));

        if (!actor.isEmpty())
        {
            if (QDir(based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + actor.getId().toActorId()).exists())
                file->open(QIODevice::WriteOnly | QIODevice::Truncate);
            else
            {
                file->close();
                delete file;
                handlerFileMutex.unlock();
                this->thread()->sleep(1);
                qDebug() << "[&DfsResolver]-[actor not empty, but directory wasn't create]";
                return createTempFile(path, size, tHash);
            }
        }
        else
        {
            file->close();
            delete file;
            handlerFileMutex.unlock();
            this->thread()->sleep(5);
            qDebug() << "[&DfsResolver]-[actor empty]";
            return createTempFile(path, size, tHash);
        }
    }
    handlerFileMutex.unlock();
    qDebug() << "[&DfsResolver] succed finished";
    return true;
}

bool ResolverService::registerTitle(const QString &tmpPath, const long long &pckgAmount,
                                    const long long &size, const QByteArray &titleSerialize,
                                    const QByteArray &tHash)
{
    if (createTempFile(tmpPath, size, tHash))
    {
        handlerFileMutex.lock();
        pckgCounter->insert(tHash, pckgAmount);
        fileMap->insert(tmpPath, titleSerialize);
        handlerFileMutex.unlock();
        qDebug() << "[ready for receive file]";
    }
    return true;
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
