#include "dfs/managers/headers/package_resolver.h"
DFSResolver::DFSResolver(ActorIndex *actorIndex, QObject *parent)
    : QObject(parent)
    , actorIndex(actorIndex)
{
}
DFSResolver::~DFSResolver()
{
    emit finished();
}

bool DFSResolver::isActive() const
{
    return active;
}

bool DFSResolver::createTempFile(const QString &path, const long long &size)
{
    QFile *file = new QFile(path);
    qDebug() << "[&DfsResolver] start create tmp file";
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QList<QByteArray> pathList = Serialization::deserialize(path.toUtf8() + '/', "/");

        Actor<KeyPublic> actor = actorIndex->getActor(BigNumber(pathList.at(PathStruct::aId)));

        if (!actor.isEmpty())
        {
            if (QDir(based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + actor.getId().toActorId()).exists())
                file->open(QIODevice::WriteOnly | QIODevice::Truncate);
            else
            {
                file->close();
                delete file;
                this->thread()->sleep(1);
                qDebug() << "[actor not empty directory wasn't create]";
                return createTempFile(path, size);
            }
        }
        else
        {
            file->close();
            delete file;
            this->thread()->sleep(5);
            qDebug() << "[actor empty]";
            return createTempFile(path, size);
        }
    }
    QByteArray zeroData = QByteArray(size, '0');
    file->write(zeroData);
    //    file->resize(size);
    file->close();
    qDebug() << "[&DfsResolver] finished";
    delete file;
    return true;
}
void DFSResolver::validate()
{
}

void DFSResolver::receiveMsg(const QByteArray &msg, int dMsgType, const SocketPair &receiver)
{
    qDebug() << "[dfs resolve message]";
    Message::dfsMessageType msgType = static_cast<Message::dfsMessageType>(dMsgType);
    // resolve msg
    if (msgType == Message::dfsMessageType::titleMessage)
    {
        qDebug() << "[title message:]";
        Message::title_message message(msg);

        QString path = message.filePath + based_dfs_struct::FILE_IDENTIFICATOR;
        qDebug() << "[file path]" << path;
        if (createTempFile(path, message.fileSize))
        {
            queueFiles.insert(message.hash(), path);
            counterPckg.insert(message.hash(), message.pckgsAmount);
            fileMap.insert(path, message.serialize());
            qDebug() << "[ready for receive file]";
        }
        else
        {
            qDebug() << "[not ready :) if you see this, you're narcoman]";
        }
    }
    else if (msgType == Message::dfsMessageType::statusMessage)
    {
        qDebug() << "[statusMessagee:]";
        Message::Status message(msg);
    }
    else if (msgType == Message::dfsMessageType::requestMessage)
    {
        qDebug() << "[requestMessage:]";
    }
    else if (msgType == Message::dfsMessageType::storageMessage)
    {
        qDebug() << "[storageMessage:]";
    }
    else if (msgType == Message::dfsMessageType::fileDataMessage)
    {
        qDebug() << "[fileDataMessage:]";
        Message::dfs_message message(msg);
        if (queueFiles.find(message.title_hash) == queueFiles.end())
        {
            qDebug() << "[hash]" << message.title_hash;
            qDebug() << "[queueFiles]" << queueFiles;
            qDebug() << "[not correct msg]";
            return;
        }
        QString path = queueFiles[message.title_hash];
        if (fileMap.find(path) == fileMap.end())
        {
            qDebug() << "[OOps we have problem]";
            return;
        }
        if (counterPckg.find(message.title_hash) == counterPckg.end())
        {
            qDebug() << "[already have finished]";
            return;
        }
        Message::title_message title(fileMap[path]);
        QFile file(path);
        file.open(QIODevice::ReadWrite);
        qDebug() << "[&receiver]" << message.pckgNumber << "[of file path=]" << title.filePath;
        file.seek(message.pckgNumber * Message::dataSize);
        if (file.write(message.data))
            counterPckg[message.title_hash]--;
        file.flush();
        file.close();
        if (counterPckg[message.title_hash] == 0)
        {
            emit save(path, title.filePath, based_dfs_struct::convertToDFType(title.f_type));
            fileMap.erase(fileMap.find(path));
            queueFiles.erase(queueFiles.find(message.title_hash));
            counterPckg.erase(counterPckg.find(message.title_hash));
        }
    }
    else if (msgType == Message::dfsMessageType::responseMessage)
    {
        qDebug() << "[fileDataMessage:]";
    }
}

void DFSResolver::process()
{
}
