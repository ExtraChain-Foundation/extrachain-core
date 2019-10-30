#include "dfs/controls/headers/dfs.h"
#include <iterator>

DFSNetManager *Dfs::getDfsNetManager() const
{
    return dfsNetManager;
}

void Dfs::setDfsNetManager(DFSNetManager *value)
{
    dfsNetManager = value;
}

void Dfs::initD(const QByteArray &userId)
{

    qDebug() << "[init dfs for user]" << userId;
    //    signalConnections();

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
    emit usersChanges(dfsPath, type, BigNumber("-2"));
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

void Dfs::statusD()
{
    if (QDir(based_dfs_struct::ROOT_FOOLDER_NAME).exists())
    {
        QDir dir(based_dfs_struct::ROOT_FOOLDER_NAME);
        QStringList list = dir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
        for (const QString &el : list)
        {
            if (el != based_dfs_struct::ACTOR_CARD_FILE)
            {
                Message::Status status(el.toUtf8(), CardManager::getAllFiles(el.toUtf8()));
                emit sendMsg(status.serialize(), Messages::DFS_MESSAGE, SocketPair());
            }
        }
    }
    else
    {
        Message::Status status("1", QStringList());
        emit sendMsg(status.serialize(), Messages::DFS_MESSAGE, SocketPair());
    }
}

void Dfs::signalConnection()
{
    connect(sender, &Sender::sendPckg, dfsNetManager, &DFSNetManager::send);
    connect(this, &Dfs::sendQ, sender, &Sender::sendFile);
    connect(resolver, &DFSResolver::save, this, &Dfs::saveFN);
    connect(this, &Dfs::resolveMsg, resolver, &DFSResolver::receiveMsg);
    connect(resolver, &DFSResolver::checkStatus, this, &Dfs::checkAc);
    connect(resolver, &DFSResolver::closingMsg, sender, &Sender::checkClosing);
    connect(resolver, &DFSResolver::initDfs, this, &Dfs::initUser);
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

    emit sendQ(path, type, SocketPair());
#ifdef ETALONIUM_CLIENT
    emit usersChanges(path.toUtf8(), type,
                      BigNumber(pathList.at(PathStruct::aId))); // TODO
#endif
}

void Dfs::checkAc(const QByteArray &actorId, const QStringList &request, const SocketPair &receiver)
{
    qDebug() << "[&Dfs] check dfs for " << actorId;
    if (actorId == "1")
    {
        QDir acDir(based_dfs_struct::ROOT_FOOLDER_NAME);
        QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
        for (const QString &el : acList)
        {
            QStringList fList = CardManager::getAllFiles(el.toUtf8());
            for (const QString &file : fList)
            {
                based_dfs_struct::Type ftype = CardManager::getTypeByName(file, el.toUtf8());
                emit sendQ(file, ftype, receiver);
            }
        }
    }
    QDir dir(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + actorId);
    if (!dir.exists())
    {
        qDebug() << "[&Dfs] Directory for actor" << actorId << "not found";
        //        emit newSender(request.serialize(), Messages::DFS_MESSAGE);
        return;
    }
    QStringList fileList = CardManager::getAllFiles(actorId);
    if (fileList != request)
    {
        for (const QString &el : fileList)
            if (!request.contains(el))
            {
                based_dfs_struct::Type type = CardManager::getTypeByName(el, actorId);
                if (type != based_dfs_struct::servic)
                    emit sendQ(el, type, receiver);
                else
                    qDebug() << "[&Dfs] the file with path" << el << "not have been found";
            }
    }
}

Dfs::Dfs(ActorIndex *actorIndex, AccountController *accControler, QObject *parent)
    : QObject(parent)
    , accountControler(accControler)
    , actorIndex(actorIndex)
{
    dfsNetManager = new DFSNetManager(accountControler, actorIndex);
}

Dfs::~Dfs()
{
}

void Dfs::savedNewData(const QString &path, const based_dfs_struct::Type &type,
                       const based_dfs_struct::SubType &subType, const based_dfs_struct::Status &status)
{
    saveD(path, type, subType, status);
}

void Dfs::process()
{
}

void Dfs::init()
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    sender = new Sender(userId);
    resolver = new DFSResolver(actorIndex);
    //
    signalConnection();
    ThreadPool::addThread(sender);
    ThreadPool::addThread(resolver);
    ThreadPool::addThread(dfsNetManager);

    statusD();
    initD(userId);
}

void Dfs::initUser(BigNumber userId)
{
    initD(userId.toActorId());
}
