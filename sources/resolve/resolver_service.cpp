#include "headers/resolve/resolver_service.h"
#include "headers/resolve/resolve_manager.h"
#include "headers/managers/node_manager.h"
#include "headers/datastorage/index/actorindex.h"
#include "headers/datastorage/blockchain.h"
#include "headers/managers/tx_manager.h"
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

void ResolverService::setDfs(Dfs *value)
{
    dfs = value;
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
    else
    {
        if (taskQueue.size() != 0)
        {
            Network::DataStruct ds = taskQueue.front();
            setTask(ds.msg, ds.receiver);
            taskQueue.pop();
            resolveTask();
        }
    }
}

void ResolverService::checkStatus()
{
    QList<QByteArray> emptyFrags;
    emptyFrags.clear();
    for (unsigned long i = 0; i < dataChecker.size(); i++)
    {
        if (!dataChecker[i])
        {
            emptyFrags.append(QByteArray::number(static_cast<long long>(i)));
        }
    }
    if (emptyFrags.isEmpty())
    {
        dfs->saveFN(file.fileName(), title.filePath, based_dfs_struct::convertToDFType(title.f_type));

        qDebug() << "[&DFSResolver][file succed written to tmp]";
        file.close();
        disconnect(reloadTimer, &QTimer::timeout, this, &ResolverService::checkStatus);
        //        delete reloadTimer;
        emit TaskFinished();
    }
    else
    {
        DFSMessage::req_frags_message reqFrags(title.filePath.toUtf8(), emptyFrags);
        dfs->dfsNetManager->send(reqFrags.serialize());
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

// QByteArray ResolverService::checkMsgType(const QByteArray &msg) const
//{

//    Messages::BaseMessage b;
//    b.deserialize(msg);
//    return b.getMsgType();
//}

QByteArray ResolverService::calcHash(const QByteArray &request) const
{
    //    qDebug() << "RESOLVER SERVICE: "
    //             << "calcHash()";
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
    if (this->type == Resolver::Type::DFS)
    {
        if (reloadTimer == nullptr)
        {
            reloadTimer = new QTimer();
            connect(reloadTimer, &QTimer::timeout, this, &ResolverService::checkStatus);
            connect(this, &ResolverService::activate, this, &ResolverService::process);
        }
    }
    resolveTask();
}

void ResolverService::assignNewTask(Network::DataStruct task)
{
    if (this->type != Resolver::Type::GENERAL)
    {
        if (active == false)
        {
            this->msg = task.msg;
            this->hash = calcHash(msg);
            this->receiver = task.receiver;
            emit activate();
        }
        else
        {
            this->taskQueue.push(task);
        }
    }
}

void ResolverService::resolveTask()
{
    switch (this->type)
    {
    case Resolver::Type::GENERAL:
        resolveGeneralTask();
        break;
    case Resolver::Type::DFS:
        reloadTimer->start(DFS_PWT);
        resolveDfsTask();
        break;
    case Resolver::Type::ACTORS:
        break;
    case Resolver::Type::BLOCKCHAIN:
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
    if (message.getMsg_data().isEmpty() && msgType != GET_ALL_ACTORS && msgType != GET_BLOCK_COUNT_MESSAGE)
    {
        emit TaskFinished();
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
                return;
        }
        else
        {
            //            qDebug() << "received msg signature:" << message.getDigSig();
            if (MessageIsNotValid(message))
                return;
        }
    }
    // dfs message
    if (msgType == DFS_MESSAGE)
    {
        qDebug() << "[&Resolver:]" << DFS_MESSAGE << "is detected";
        DFSMessage::DUMessage dfsMsg(message.getMsg_data());
        resolveDfsMessage(message.getMsg_data(), dfsMsg.getType(), receiver);
        finishWork();
        //        emit TaskFinished();
    }
    else if ((msgType == INVITE_CHAT_MESSAGE) || (msgType == CHAT_MESSAGE))
    {
        //
        chatManager->msgReceiver(message);
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
        //        GetAllActorMessage response(message.getMsg_data());
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
        //        qDebug() << "RESOLVER SERVICE: "
        //                 << "recieveMsg(): type: " << GET_ALL_ACTORS_RESPONSE_MESSAGE << "\nmessage: " <<
        //                 msg;
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

void ResolverService::resolveDfsTask()
{
    using namespace Messages;
    // dfs message
    qDebug() << "[&Resolver:]" << DFS_MESSAGE << "is detected";
    DFSMessage::DUMessage dfsMsg(msg);
    resolveDfsMessage(msg, dfsMsg.getType(), receiver);
    //        emit TaskFinished();
    finishWork();
}
void ResolverService::resolveDfsMessage(const QByteArray &data, const int &mType, const SocketPair &receiver)
{
    //    emit restartLoadChecker();
    qDebug() << "[dfs resolve message]";
    Network::DataStruct ds;
    ds.msg = data;
    ds.receiver = receiver;
    DFSMessage::dfsMessageType msgType = static_cast<DFSMessage::dfsMessageType>(mType);
    // resolve msg
    if (msgType == DFSMessage::dfsMessageType::requestFragments)
    {
        DFSMessage::req_frags_message message(data);
        dfs->resendFragments(message.getFilePath(), message.getListFrag());
    }
    else if (msgType == DFSMessage::dfsMessageType::titleMessage)
    {
        if (type == Resolver::Type::GENERAL)
        {
            qDebug() << "[title message:]";
            DFSMessage::title_message message(data);
            QByteArray mHash = message.dataHash;
            if (QFile(message.filePath).exists())
            {
                //                finishWork();
                return;
            }
            emit dfsTitle(mHash, ds);
            //            resolveManager->dfsTitleArrived(mHash, ds);
        }
        else if (type == Resolver::Type::DFS)
        {
            DFSMessage::title_message message(data);
            QString path = message.filePath + based_dfs_struct::FILE_IDENTIFICATOR;
            if (!registerTitle(path, message))
            {
                qDebug() << "Title was not registered";
                //                finishWork();
                return;
            }
            // register title for receive
        }
    }
    else if (msgType == DFSMessage::dfsMessageType::statusMessage)
    {
        qDebug() << "[statusMessagee:]";
        DFSMessage::Status message(data);
    }
    else if (msgType == DFSMessage::dfsMessageType::requestMessage)
    {
        qDebug() << "[requestMessage:]";
        DFSMessage::dfs_request message(data);
        dfs->fileResponse(message.filePath, receiver);
    }
    else if (msgType == DFSMessage::dfsMessageType::storageMessage)
    {
        qDebug() << "[storageMessage:]";
    }
    else if (msgType == DFSMessage::dfsMessageType::fileDataMessage)
    {
        if (type == Resolver::Type::GENERAL)
        {
            DFSMessage::dfs_message message(data);
            emit dfsFragment(message.dataHash, ds);
            //            resolveManager->dfsFragmentArrived(message.dataHash, ds);
        }
        else if (type == Resolver::Type::DFS)
        {
            qDebug() << "[fileDataMessage:]";
            DFSMessage::dfs_message message(data);
            file.seek(DFSMessage::dataSize * message.pckgNumber);
            file.write(message.data);
            file.flush();
            dataChecker[message.pckgNumber] = true;
            reloadTimer->start(DFS_PWT);
        }
    }
    else if (msgType == DFSMessage::dfsMessageType::responseMessage)
    {
        qDebug() << "[responseMessage:]";
    }
    else if (msgType == DFSMessage::dfsMessageType::closingMessage)
    {
        qDebug() << "[closingMessage:]";
        DFSMessage::DClosing message(msg);
        //        emit closingMsg(message.title_hash, message.PckgAmoutR, receiver);
    }
    else
        qDebug() << "[&DFSResolver] undifine message type";
    //    finishWork();
}

bool ResolverService::createTempFile(const QString &path, const long long &size, const QByteArray &tHash)
{
    qDebug() << "[&DfsResolver] start create tmp file:" << path;
    //    handlerFileMutex.lock();
    file.setFileName(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        // Take actorid of file owner
        QList<QByteArray> pathList = Serialization::deserialize(path.toUtf8() + '/', "/");
        qDebug() << "Create temp file: actor - " << BigNumber(pathList.at(PathStruct::aId));
        Actor<KeyPublic> actor = actorIndex->getActor(BigNumber(pathList.at(PathStruct::aId)));

        if (!actor.isEmpty())
        {
            if (QDir(based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + actor.getId().toActorId()).exists())
                file.open(QIODevice::WriteOnly | QIODevice::Truncate);
            else
            {
                qDebug() << "[&DfsResolver]-[actor not empty, but directory wasn't create]";
                return createTempFile(path, size, tHash);
            }
        }
        else
        {
            file.close();
            this->thread()->sleep(5);
            qDebug() << "[&DfsResolver]-[actor empty]";
            return createTempFile(path, size, tHash);
        }
    }
    if (file.isOpen())
    {
        for (long long i = 0; i < size; i++)
        {
            file.seek(i);
            file.write("0");
        }
    }
    //    handlerFileMutex.unlock();
    qDebug() << "[&DfsResolver] succed finished";
    return true;
}

bool ResolverService::registerTitle(const QString &tmpPath, DFSMessage::title_message title)
{
    if (this->title.empty())
    {
        this->title = title;
        if (createTempFile(tmpPath, title.fileSize, title.dataHash))
        {
            dataChecker.assign(title.pckgsAmount, false);
            qDebug() << "[ready to receive file]";
        }
        return true;
    }
    else
        return false;
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
