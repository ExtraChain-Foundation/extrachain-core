#include "headers/resolve/dfs_resolver_service.h"
#include "managers/node_manager.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/blockchain.h"
#include "managers/tx_manager.h"
#include "dfs/controls/headers/dfs.h"
#include "managers/chatmanager.h"
#include "managers/account_controller.h"

void DFSResolverService::setTitle(const DFSMessage::title_message &value)
{
    title = value;
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
    qDebug() << "emptyFrags:" << emptyFrags;
    return emptyFrags;
}

void DFSResolverService::checkStatus()
{
    qDebug() << QObject::sender();
    QByteArray emptyFrags = checkFragStatus(reqStart, reqFin);
    if (emptyFrags.isEmpty() && reqStart >= dataChecker.size())
    {
        file.close();
        dfs->saveFN(file.fileName(), title.filePath, dfsStruct::convertToDFType(title.f_type));

        qDebug() << "[&DFSResolver][file succed written to tmp]";

        disconnect(reloadTimer, &QTimer::timeout, this, &DFSResolverService::checkStatus);
        //        reloadTimer->deleteLater();
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
        }
        else
        {
            DFSMessage::req_frags_message reqFrags(title.filePath.toUtf8(), emptyFrags);
            dfs->dfsNetManager->send(reqFrags.serialize());
        }
    }
}

bool DFSResolverService::isActive() const
{
    return active;
}

void DFSResolverService::setTask(QByteArray msg, SocketPair receiver)
{
    active = true;
    this->msg = msg;
    this->hash = Utils::calcKeccak(msg);
    this->receiver = receiver;
}

bool DFSResolverService::validate(const Messages::IMessage &message)
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
    //    if (reloadTimer == nullptr)
    //    {
    //        reloadTimer = new QTimer();
    //        connect(reloadTimer, &QTimer::timeout, this, &DFSResolverService::checkStatus);
    //    }
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
        qDebug() << "[&Resolver:]" << DFS_MESSAGE << "is detected";
        BaseMessage bmsg;
        bmsg.deserialize(msg);
        DFSMessage::DUMessage dfsMsg(bmsg.getData());
        if (dfsMsg.isEmpty())
            return;
        resolveDfsMessage(bmsg.getData(), dfsMsg.getType());
        //        emit TaskFinished();
    }
    //    finishWork();
}
void DFSResolverService::resolveDfsMessage(const QByteArray &data, const int &mType)
{
    //    qDebug() << "[dfs resolve message] msg type:" << mType;
    DFSMessage::dfsMessageType msgType = static_cast<DFSMessage::dfsMessageType>(mType);
    using namespace DFSMessage;
    if (this->lifetime == Resolver::Lifetime::SHORT)
    {
        switch (msgType)
        {
        case dfsMessageType::titleMessage:
        {
            Network::DataStruct ds = { this->msg, this->receiver };
            emit dfsTitle(ds);
            break;
        }
        case dfsMessageType::requestFragments:
        {
            DFSMessage::req_frags_message message(data);
            if (message.filePath == "-1")
                return;
            dfs->sendFragments(message.getFilePath(), message.getListFrag(), this->receiver);
            break;
        }
        case dfsMessageType::requestMessage:
        {
            qDebug() << "[requestMessage:]";
            DFSMessage::dfs_request message(data);
            dfs->fileResponse(message.filePath, receiver);
            break;
        }
        case dfsMessageType::responseMessage:
        {
            qDebug() << "[responseMessage:]";
            break;
        }
        case dfsMessageType::statusMessage:
        {
            qDebug() << "[statusMessagee:]";
            DFSMessage::Status message(data);
            break;
        }
        case dfsMessageType::storageMessage:
        {
            qDebug() << "[storageMessage:]";
            break;
        }
        case dfsMessageType::closingMessage:
        {
            break;
        }
        default:
        {
            qDebug() << "[&DFSResolver] undifined message type from LIFETIME::SHORT";
            break;
        }
        }
    }
    else if (this->lifetime == Resolver::Lifetime::LONG)
    {
        switch (msgType)
        {
        case dfsMessageType::titleMessage:
        {
            if (title.empty())
            {
                DFSMessage::title_message message(data);
                QString path = message.filePath + dfsStruct::FILE_IDENTIFICATOR;
                if (QFile::exists(message.filePath))
                {
                    finishWork();
                    return;
                }
                if (!registerTitle(path, message))
                {
                    qDebug() << "Title was not registered";
                    //                finishWork();
                    active = false;
                    return;
                }
                reloadTimer->start(Network::DFS_FILE_STATUS_CHECK_TIME);
            }
            break;
        }
        case dfsMessageType::fileDataMessage:
        {
            qDebug() << "[fileDataMessage:]";
            DFSMessage::dfs_message message(data);
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
            file.seek(DFSMessage::dataSize * message.pckgNumber);
            file.write(message.data);
            file.flush();
            //            mutex.unlock();
            //            qDebug() << message.pckgNumber;
            dataChecker[std::size_t(message.pckgNumber)] = true;
            reloadTimer->stop();
            reloadTimer->start(Network::DFS_FILE_STATUS_CHECK_TIME);
            break;
        }
        default:
        {
            qDebug() << "[&DFSResolver] undifined message type from LIFETIME::LONG";
            break;
        }
        }
        active = false;
    }
}

bool DFSResolverService::createTempFile(const QString &path, const long long &size, const QByteArray &tHash)
{
    qDebug() << "[&DfsResolver] start create tmp file:" << path;
    //    handlerFileMutex.lock();
    QString dirPath = path.mid(0, path.lastIndexOf("/") + 1);
    QDir dir;
    dir.mkdir(dirPath);
    file.setFileName(path);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Truncate))
    {
        // Take actorid of file owner
        QList<QByteArray> pathList = Serialization::deserialize(path.toUtf8() + '/', "/");
        qDebug() << "Create temp file: actor - " << BigNumber(pathList.at(PathStruct::aId));
        Actor<KeyPublic> actor = actorIndex->getActor(BigNumber(pathList.at(PathStruct::aId)));

        if (!actor.isEmpty())
        {
            if (QDir(dfsStruct::ROOT_FOOLDER_NAME.toUtf8() + '/' + actor.getId().toActorId()).exists())
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

bool DFSResolverService::registerTitle(const QString &tmpPath, DFSMessage::title_message title)
{
    if (this->title.empty())
    {
        this->title = title;
        if (createTempFile(tmpPath, title.fileSize, title.dataHash))
        {
            dataChecker.assign(title.pckgsAmount, false);
            qDebug() << "[ready to receive file]" << title.filePath;
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
        qDebug() << "[NOT ready to receive file (title error)]" << title.filePath;
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

DFSMessage::title_message DFSResolverService::getTitle() const
{
    return title;
}
