#include "headers/resolve/dfs_resolver_service.h"
#include "managers/node_manager.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/blockchain.h"
#include "managers/tx_manager.h"
#include "dfs/controls/headers/dfs.h"
#include "managers/chatmanager.h"
#include "managers/account_controller.h"

void DFSResolverService::setTitle(const DistFileSystem::TitleMessage &value)
{
    title = value;
}

void DFSResolverService::setActorIndex(ActorIndex *value)
{
    actorIndex = value;
}

DFSResolverService::DFSResolverService(Lifetime lifetime, QObject *parent)
    : QObject(parent)
{
    this->lifetime = lifetime;
}

DFSResolverService::~DFSResolverService()
{
    //    emit finished();
}

void DFSResolverService::finishWork()
{
    active = false;
    emit TaskFinished();
}

QByteArray DFSResolverService::checkFragStatus(unsigned long from, unsigned long to)
{
    // qDebug() << "checkFragStatus" << dataChecker.size();
    QByteArray emptyFrags;
    unsigned long s = ULONG_MAX;
    unsigned long e = ULONG_MAX;
    for (unsigned long i = from; i <= to; i++)
    {
        if (!dataChecker[i])
        {
            if (s == ULONG_MAX)
            {
                s = i;
            }
            else
            {
                e = i;
            }
        }

        if (dataChecker[i] || i == to)
        {
            if (s != ULONG_MAX && e == ULONG_MAX)
                emptyFrags +=
                    (emptyFrags.isEmpty() ? "" : " ") + QByteArray::number(static_cast<long long>(s));

            if (s != ULONG_MAX && e != ULONG_MAX)
                emptyFrags += (emptyFrags.isEmpty() ? "" : " ")
                    + QByteArray::number(static_cast<long long>(s)) + ":"
                    + QByteArray::number(static_cast<long long>(e));

            s = ULONG_MAX;
            e = ULONG_MAX;
        }

        // 5:8 14 16:54 66
    }
    // qDebug() << "emptyFrags:" << emptyFrags;
    return emptyFrags;
}

void DFSResolverService::checkStatus()
{
    if (title.filePath.indexOf("root") != -1)
    {
        qDebug() << "root";
    }

    QByteArray emptyFrags = checkFragStatus(reqStart, reqFin);
    if (emptyFrags.isEmpty() && reqStart >= dataChecker.size())
    {
        file.close();
        dfs->save(DfsStruct::DfsSave::Network, title.filePath, "", (DfsStruct::Type)title.f_type);

        qDebug() << "[&DFSResolver][file succed written to tmp]";

        if (reloadTimer != nullptr)
        {
            disconnect(reloadTimer, &QTimer::timeout, this, &DFSResolverService::checkStatus);
            reloadTimer->deleteLater();
        }
        finishWork();
    }
    else
    {
        if (emptyFrags.isEmpty())
        {
            reqStart = reqFin + 1;
            reqFin = reqFin + Network::FRAGMENT_STACK_SIZE;
            if (reqFin > dataChecker.size() - 1)
                reqFin = dataChecker.size() - 1;
            //            checkStatus();
        }
        else
        {
            DistFileSystem::ReqFragsMessage reqFrags;
            reqFrags.filePath = title.filePath.toUtf8();
            reqFrags.listFrag = emptyFrags;
            dfs->dfsNetManager->send(reqFrags.serialize(), Messages::DFSMessage::requestFragments);
        }
    }
}

bool DFSResolverService::isActive() const
{
    return active;
}

void DFSResolverService::setTask(QByteArray _msg, SocketPair _receiver)
{
    active = true;
    this->msg = _msg;
    this->hash = Utils::calcKeccak(msg);
    this->receiver = _receiver;
}

bool DFSResolverService::validate(const Messages::BaseMessage &message)
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
        this->thread()->sleep(5);
        return validate(message);
    }
}

void DFSResolverService::process()
{
    if (this->lifetime == Resolver::Lifetime::LONG)
    {
        if (reloadTimer == nullptr)
        {
            reloadTimer = new QTimer(this);
            qDebug() << reloadTimer;
            connect(reloadTimer, &QTimer::timeout, this, &DFSResolverService::checkStatus,
                    Qt::QueuedConnection);
        }
    }
    resolveDfsTask();
}

void DFSResolverService::assignNewTask(Network::DataStruct task)
{
    if (task.msg == "")
    {
        return;
    }
    if (reloadTimer == nullptr)
    {
        reloadTimer = new QTimer();
        connect(reloadTimer, &QTimer::timeout, this, &DFSResolverService::checkStatus);
    }
    active = true;
    this->msg = task.msg;
    this->hash = Utils::calcKeccak(msg);
    this->receiver = task.receiver;
    resolveDfsTask();
}

void DFSResolverService::resolveDfsTask()
{
    using namespace Messages;
    // dfs message

    if (msg != "")
    {
        // qDebug() << "[&Resolver:]" << DFS_MESSAGE << "is detected";
        BaseMessage bmsg;
        bmsg.deserialize(msg);
        if (bmsg.isEmpty())
            return;
        resolveDfsMessage(bmsg.data, bmsg.type);
        //        emit TaskFinished();
    }
    //    finishWork();
}
void DFSResolverService::resolveDfsMessage(QByteArray &data, const unsigned int &msgType)
{
    //    qDebug() << "[dfs resolve message] msg type:" << mType;
    if (Messages::isDFSMessage(msgType))
    {
        using namespace Messages;

        if (this->lifetime == Resolver::Lifetime::SHORT)
        {
            switch (msgType)
            {
            case DFSMessage::titleMessage: {
                Network::DataStruct ds = { this->msg, this->receiver };
                emit dfsTitle(ds);
                break;
            }
            case DFSMessage::requestFragments: {
                DistFileSystem::ReqFragsMessage message;
                message = data;
                if (message.filePath == "-1")
                    return;
                dfs->sendFragments(message.filePath, message.listFrag, this->receiver);
                break;
            }
            case DFSMessage::requestMessage: {
                qDebug() << "[requestMessage:]";
                DistFileSystem::DfsRequest message;
                message = data;

                if (!QFile::exists(message.filePath))
                {
                    // dfs->getSender()->sendDfsMessage(message); // TODO
                    return;
                }

                dfs->fileResponse(message.filePath, receiver);

                break;
            }
            case DFSMessage::responseMessage: {
                qDebug() << "[responseMessage:]";
                break;
            }
            case DFSMessage::statusMessage: {
                qDebug() << "[statusMessage:]";
                DistFileSystem::Status message;
                message = data;
                break;
            }
            case DFSMessage::storageMessage: {
                qDebug() << "[storageMessage:]";
                break;
            }
            case DFSMessage::closingMessage: {
                break;
            }
            case DFSMessage::changesMessage: {
                DistFileSystem::DfsChanges message;
                message = data;

                // if resolver with message.filePath exists
                // not apply && remove resolver && resend request

                if (dfs->applyChanges(message))
                    dfs->getSender()->sendDfsMessage(message, Messages::DFSMessage::changesMessage);
                break;
            }
            case DFSMessage::requestLast: {
                DistFileSystem::requestLast requestLast;
                requestLast = data;
                dfs->responseRequestLast(requestLast, this->receiver);
                break;
            }
            case DFSMessage::responseLast: {
                DistFileSystem::responseLast responseLast;
                responseLast = data;
                dfs->responseResponseLast(responseLast, receiver);
                break;
            }
            case DFSMessage::cardFileChange: {
                DistFileSystem::CardFileChange cardFileChange;
                cardFileChange = data;
                dfs->applyCardFileChange(cardFileChange, receiver);
                break;
            }
            case DFSMessage::requestCardPath: {
                DistFileSystem::RequestCardPart requestCardPath;
                requestCardPath = data;
                dfs->responseRequestCardPath(requestCardPath, receiver);
                break;
            }
            case DFSMessage::responseCardPath: {
                DistFileSystem::ResponseCardPart responseCardPath;
                responseCardPath = data;
                dfs->responseResponseCardPath(responseCardPath, receiver);
                break;
            }
            default: {
                // qDebug() << "[&DFSResolver] undefined message type from LIFETIME::SHORT";
                break;
            }
            }
        }
        else if (this->lifetime == Resolver::Lifetime::LONG)
        {
            switch (msgType)
            {
            case DFSMessage::titleMessage: {
                if (title.isEmpty())
                {
                    DistFileSystem::TitleMessage message;
                    message = data;
                    if (message.filePath.isEmpty())
                    {
                        return;
                    }

                    dfs->titleReceived(message.filePath);
                    QString path = message.filePath + DfsStruct::FILE_IDENTIFICATOR;
                    if (QFile::exists(message.filePath)
                        && (message.filePath.right(7) != ".stored" && message.filePath.right(5) != "/root"))
                    {
                        finishWork();
                        return;
                    }
                    if (!registerTitle(path, message))
                    {
                        qDebug() << "Title was not registered";
                        active = false;
                        // finishWork();
                        return;
                    }

                    if (reloadTimer != NULL)
                        reloadTimer->start(Network::DFS_FILE_STATUS_CHECK_TIME);
                    else
                        qDebug() << "timer error in title LONG";
                }
                break;
            }
            case DFSMessage::fileDataMessage: {
                // qDebug() << "[fileDataMessage:]";
                DistFileSystem::DfsMessage message;
                message = data;
                if (message.data.isEmpty())
                {
                    active = false;
                    return;
                }
                if (message.dataHash != title.dataHash)
                {
                    active = false;
                    return;
                }
                if (dataChecker[std::size_t(message.pckgNumber)])
                {
                    active = false;
                    return;
                }
                //            mutex.lock();
                file.seek(DistFileSystem::dataSize * message.pckgNumber);
                file.write(message.data);
                file.flush();
                //            mutex.unlock();
                //            qDebug() << message.pckgNumber;
                dataChecker[std::size_t(message.pckgNumber)] = true;
                reloadTimer->stop();
                reloadTimer->start(Network::DFS_FILE_STATUS_CHECK_TIME);
                break;
            }
            default: {
                // qDebug() << "[&DFSResolver] undefined message type from LIFETIME::LONG";
                break;
            }
            }
            active = false;
        }
    }
}

bool DFSResolverService::createTempFile(const QString &path, const long long &size, const QByteArray &tHash)
{
    qDebug() << "[&DfsResolver] start create tmp file:" << path;
    //    handlerFileMutex.lock();
    QString dirPath = path.mid(0, path.lastIndexOf("/") + 1);
    QDir dir;
    dir.mkpath(dirPath);
    file.setFileName(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        // Take actorid of file owner
        QList<QByteArray> pathList = Serialization::deserialize(path.toUtf8() + '/', "/");
        if (pathList.length() < 2)
            return false;
        qDebug() << "Create temp file: actor - " << BigNumber(pathList.at(PathStruct::aId));
        Actor<KeyPublic> actor = actorIndex->getActor(BigNumber(pathList.at(PathStruct::aId)));

        if (!actor.isEmpty())
        {
            if (QDir(DfsStruct::ROOT_FOOLDER_NAME.toUtf8() + '/' + actor.getId().toActorId()).exists())
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

    //    handlerFileMutex.unlock();
    qDebug() << "[&DfsResolver] succed finished" << path;
    return true;
}

bool DFSResolverService::registerTitle(const QString &tmpPath, DistFileSystem::TitleMessage message)
{
    if (this->title.isEmpty())
    {
        this->title = message;
        if (createTempFile(tmpPath, message.fileSize, message.dataHash))
        {
            dataChecker.assign(message.pckgsAmount, false);
            qDebug() << "Ready to receive file:" << message.filePath;
        }
        else
        {
            qDebug() << "[temp file was not created]";
            return false;
        }
        // qDebug() << "[NOT ready to receive file]" << title.filePath;
        return true;
    }
    else
    {
        qDebug() << "[NOT ready to receive file (title error)]" << message.filePath;
        return false;
    }
}

void DFSResolverService::setDfs(Dfs *value)
{
    dfs = value;
}

Resolver::Type DFSResolverService::getType() const
{
    return type;
}

void DFSResolverService::setType(const Resolver::Type &value)
{
    type = value;
}

void DFSResolverService::setLifetime(const Lifetime &value)
{
    lifetime = value;
}

Resolver::Lifetime DFSResolverService::getLifetime() const
{
    return lifetime;
}

DistFileSystem::TitleMessage DFSResolverService::getTitle() const
{
    return title;
}
