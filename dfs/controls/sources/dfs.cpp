#include "dfs/controls/headers/dfs.h"
#include <iterator>
Sender *Dfs::getSender() const
{
    return sender;
}

void Dfs::signalConnections()
{
}

void Dfs::initD(const QByteArray &userId)
{

    qDebug() << "[init dfs for user]" << userId;
    //    signalConnections();
    if (QDir(based_dfs_struct::ROOT_FOOLDER_NAME).exists())
    {
        QDir dir(based_dfs_struct::ROOT_FOOLDER_NAME);
        QStringList list = dir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
        for (const QString &el : list)
        {
            if (el != based_dfs_struct::ACTOR_CARD_FILE)
            {
                //                Message::Status status(el.toUtf8(), CardManager::getAllFiles(el.toUtf8()));
                //                emit newSender(status.serialize(), Messages::DFS_MESSAGE);
            }
        }
    }
    if (QDir(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId).exists())
    {
        //        Message::Status status(userId, CardManager::getAllFiles(userId));
        //        emit newSender(status.serialize(), Messages::DFS_MESSAGE);
    }
    QDir().mkdir(based_dfs_struct::ROOT_FOOLDER_NAME);
    QDir().mkdir(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId);
    QFile(based_dfs_struct::ACTOR_CARD_FILE).open(QIODevice::WriteOnly | QIODevice::Truncate);
    qDebug() << "[init finished]";
}

void Dfs::saveD(const QString &path, const based_dfs_struct::Type &type,
                const based_dfs_struct::SubType &subType, const based_dfs_struct::Status &status)
{
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();
    file.close();
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QByteArray name = setName(userId);
    QByteArray dfsPath = based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/' + name;
    appendC(dfsPath, userId, name, based_dfs_struct::toByteArray(type));
    QFile dfsFile(dfsPath);
    dfsFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    dfsFile.write(data);
    dfsFile.flush();
    dfsFile.close();
    emit sendQ(dfsPath, type, SocketPair());
#ifdef ETALONIUM_CLIENT
    emit usersChanges(dfsPath, based_dfs_struct::Type::system,
                      BigNumber("-2")); // TODO
#endif
}

void Dfs::appendC(const QString &path, const QByteArray &userId, const QByteArray &name,
                  const QByteArray &type)
{
    QFile card(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/' + based_dfs_struct::ACTOR_CARD_FILE);
    card.open(QIODevice::WriteOnly | QIODevice::Append);
    QByteArray strToWrite = Serialization::serialize({ path.toUtf8(), userId, name, type },
                                                     Serialization::DFS_CARD_FILE_SECTION_DELIMETR);
    strToWrite.append(Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    card.write(strToWrite);
    card.close();
}

QByteArray Dfs::setName(const QByteArray &userId)
{
    QFile file(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + '/' + based_dfs_struct::ACTOR_CARD_FILE);
    if (file.open(QIODevice::ReadOnly))
    {
        QList<QByteArray> list =
            Serialization::deserialize(file.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
        file.close();
        if (list.isEmpty())
            return "1";
        BigNumber r(
            Serialization::deserialize(list.takeLast(), Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2));
        return r++.toByteArray();
    }
    else
        return "1";
}

void Dfs::saveFN(const QString tmpPath, const QString &path, const based_dfs_struct::Type &type)
{
    QFile file(tmpPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "Fuck, fucking frick, where are files";
        return;
    }
    QByteArray data = file.readAll();
    file.close();
    file.remove();
    QFile result(path);
    result.open(QIODevice::WriteOnly | QIODevice::Truncate);
    result.write(data);
    result.flush();
    result.close();
    QList<QByteArray> pathList = Serialization::deserialize(path.toUtf8() + '/', "/");

    appendC(path, pathList.at(PathStruct::aId), pathList.at(PathStruct::name),
            based_dfs_struct::toByteArray(type));
#ifdef ETALONIUM_CLIENT
    emit usersChanges(path.toUtf8(), type,
                      BigNumber(pathList.at(PathStruct::aId))); // TODO
#endif
}

Dfs::Dfs(ActorIndex *actorIndex, AccountController *accControler, QObject *parent)
    : QObject(parent)
    , accountControler(accControler)
    , actorIndex(actorIndex)
{
}

Dfs::~Dfs()
{
    // save last changes and last data of users -> in file user data
    delete dfsIndex;
}

DfsIndex *Dfs::getDfsIndex() const
{
    return this->dfsIndex;
}

void Dfs::savedNewData(const QString &path, const based_dfs_struct::Type &type,
                       const based_dfs_struct::SubType &subType, const based_dfs_struct::Status &status)
{
    saveD(path, type, subType, status);
}

// void Dfs::downloadRequset(QByteArray header, QString peerAddress)
//{
//    QList<QByteArray> list = Serialization::deserialize(header, Serialization::DFS_STORED_DELIMETR);
//    if (!QFile(list.at(3)).exists())
//        emit downloadResponse(true, header, peerAddress);
//}
void Dfs::init()
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    initD(userId);
    sender = new Sender(userId);
    resolver = new DFSResolver(actorIndex);
    //
    connect(this, &Dfs::sendQ, sender, &Sender::sendFile);
    connect(sender, &Sender::sendS, this, &Dfs::newSender);
    connect(sender, &Sender::sendToPeer, this, &Dfs::newSenderToPeer);
    connect(resolver, &DFSResolver::save, this, &Dfs::saveFN);
    connect(this, &Dfs::resolveMsg, resolver, &DFSResolver::receiveMsg,
            Qt::ConnectionType::BlockingQueuedConnection);
    ThreadPool::addThread(sender);
    ThreadPool::addThread(resolver);
}

void Dfs::initUser(BigNumber userId)
{
    initD(userId.toActorId());
}

void Dfs::receive(const QByteArray &data, const int &msgType, const SocketPair &receiver)
{
    emit resolveMsg(data, msgType, receiver);
}

void Dfs::process()
{
}

void Dfs::appendSubscribtion(const BigNumber &actorId)
{
    Subscribtion sub;
    sub.add(actorId);
}

// void Dfs::statusResponse(const QByteArray &data, const SocketPair &receiver)
//{
//    Message::Status message(data);
//    QStringList list = CardManager::getAllFiles(message.dirOwner);
//    if (message.currentState != list)
//    {
//        for (const QString &el : list)
//            if (!message.currentState.contains(el))
//                dfsSender(el, receiver);
//    }
//    //    for (const QString &el : list)
//}
