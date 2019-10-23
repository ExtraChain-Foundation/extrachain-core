#include "dfs/controls/headers/dfs.h"
#include <iterator>
void Dfs::signalConnections()
{
    // qDebug() << connect(dfsIndex, &DfsIndex::sendData, this, &Dfs::sendMessage);
    // qDebug() << "dfs request connection" << connect(dfsIndex, &DfsIndex::sendToUser, this,
    // &Dfs::sendToPeer);
    //    connect(dfsIndex, &DfsIndex::sendRequest, this, &Dfs::sendRequestf);
}

void Dfs::initD(const QByteArray &userId)
{
    qDebug() << "[init dfs for user]" << userId;
    signalConnections();
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
    file.open(QIODevice::ReadOnly);

    QList<QByteArray> list =
        Serialization::deserialize(file.readAll(), Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
    if (list.isEmpty())
        return "1";
    BigNumber r(
        Serialization::deserialize(list.takeLast(), Serialization::DFS_CARD_FILE_SECTION_DELIMETR).at(2));
    return r++.toByteArray();
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
    //    QString createdPath = based_dfs_struct::ROOT_FOOLDER_NAME + '/'
    //        + accountControler->getMainActor()->getId().toActorId() + '/' + based_dfs_struct::toString(type)
    //        + '/';
    //    createdPath += based_dfs_struct::images == type ? based_dfs_struct::toString(subType) + '/' : "";
    //    if (type != based_dfs_struct::servic)
    //        createdPath += CardManager::getNameForNewFile(type).toByteArray();
    //    else
    //    {
    //        if (subType == based_dfs_struct::profil)
    //            createdPath += "profile.dat";
    //        else
    //            createdPath += "avatar";
    //    }
    //    createdPath += '&' + path;
    //    dfsIndex->changedData(createdPath, type, subType, status);

    //#ifdef ETALONIUM_CLIENT
    //    emit usersChanges(path.toUtf8(), based_dfs_struct::Type::system, BigNumber("-2")); // TODO
    //#endif
}

void Dfs::downloadRequset(QByteArray header, QString peerAddress)
{
    QList<QByteArray> list = Serialization::deserialize(header, Serialization::DFS_STORED_DELIMETR);
    if (!QFile(list.at(3)).exists())
        emit downloadResponse(true, header, peerAddress);
}
void Dfs::init()
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    initD(userId);
    //    dfsIndex = new DfsIndex(actorIndex, accountControler);
    //    signalConnections();
    //    BigNumber id = accountControler->getMainActor() == nullptr ? BigNumber(-1)
    //                                                               : accountControler->getMainActor()->getId();
    //    CardManager::createdCardFilesConnection(id);
    //    int error = CardManager::checkDfsState(id);
    //    if (error == -1)
    //    {
    //        dfsIndex->makeSystemDir(accountControler->getMainActor()->getId());
    //    }
    //    else
    //    {
    //        Message::getStatus request;
    //        emit newSender(request.request, Messages::DFS_STATUS_MESSAGE);
    //    }
    //    qDebug() << "[Dfs]:: dfs has been init";
    //    emit beginTest();
}

void Dfs::initUser(BigNumber userId)
{
    initD(userId.toActorId());
    //    dfsIndex->makeSystemDir(userId);
    //    CardManager::createdAllCards(userId);
    //    qDebug() << "init start dfs for user - " << userId.toActorId();
}

void Dfs::getUserDataAnswer(int request, QByteArray data)
{
    switch (request)
    {
    case DFS_REQUESTS::GET_USER_ID:
    {
        UsersData<BigNumber> temp(data);
        return;
    }
    case DFS_REQUESTS::GET_MY_PRIVATE_KEY:
    {
        UsersData<Actor<KeyPrivate>> type(data);
        return;
    }
    case DFS_REQUESTS::GET_USER_PUBLIC_KEY:
        UsersData<Actor<KeyPublic>> type(data);
        return;
    }
}

void Dfs::recieve(Messages::DfsMessage msg)
{
    emit sendMessage(msg);
    QString fileName = msg.getFilePath() + based_dfs_struct::FILE_IDENTIFICATOR;
    QList<QByteArray> pathList = Serialization::deserialize(msg.getFilePath().toUtf8() + '/', "/");

    bool isCardFile = false;

    //    based_dfs_struct::typeCardFilesMap<based_dfs_struct::Type, QString>::
    //    std::for_each(based_dfs_struct::typesVec.end(), based_dfs_struct::typesVec.end(),
    //                  [&isCardFile, &pathList](based_dfs_struct::Type el) {
    //                      QString r = based_dfs_struct::typeCardFilesMap.find(el)->second;
    //                      if (r == pathList.last())
    //                          isCardFile = true;
    //                  });

    Actor<KeyPublic> actor = actorIndex->getActor(BigNumber(pathList[1]));
    if (actor.isEmpty())
    {
        if (!QDir(temp_History).exists())
        {
            QDir dir;
            dir.mkdir(temp_History);
        }
        if (filesQueue.find(Utils::calcKeccak(fileName.toUtf8())) == filesQueue.end())
            filesQueue[Utils::calcKeccak(fileName.toUtf8())] = fileName;
    }

    QFile file;
    if (filesQueue.find(Utils::calcKeccak(fileName.toUtf8())) != filesQueue.end())
        file.setFileName(temp_History + Utils::calcKeccak(fileName.toUtf8()));
    else
        file.setFileName(fileName);

    if (msg.getPackageNumber() == 0)
        if (file.exists())
            file.remove();
    if (isCardFile)
    {
        file.setFileName(pathList.last() + based_dfs_struct::FILE_IDENTIFICATOR);
        if ((msg.getSize() == file.size()) || (msg.getPackageNumber() == (msg.getNeedsByteCount() - 1)))
        {
            QStringList requestList =
                dfsIndex->fileCompareAndReturnDifference(file.fileName(), msg.getFilePath());

            return;
        }
    }
    else
    {
        if (QFile(msg.getFilePath()).exists())
            return;
    }
    file.open(QIODevice::WriteOnly | QIODevice::Append);
    file.seek(msg.getPackageNumber() * 512);
    file.write(msg.getData());
    file.flush();
    file.close();
    long long size = file.size();

    if ((msg.getSize() == size) || (msg.getPackageNumber() == (msg.getNeedsByteCount() - 1)))
    {
        if (!actor.isEmpty())
        {
            saveD(fileName);
            //            if (filesQueue.find(Utils::calcKeccak(fileName.toUtf8())) != filesQueue.end())
            //            {
            //                QFile::rename(temp_History + Utils::calcKeccak(fileName.toUtf8()), fileName);
            //            }
            //            DfsItem dfsItem(fileName, based_dfs_struct::Status::REPLACE, actorIndex,
            //            accountControler,
            //                            msg.getData());
            //            CardManager::appendToCard(dfsItem.getType(), dfsItem.serialize(),
            //            dfsItem.getActorId()); emit usersChanges(dfsItem.getPath(), dfsItem.getType(),
            //            dfsItem.getActorId());
        }

        file.remove();
    }
    else
    {
        //        qDebug() << "the number of packages wrong";
    }
    if (QFile(msg.getFilePath()).exists())
        QFile(msg.getFilePath() + based_dfs_struct::FILE_IDENTIFICATOR).remove();
}

void Dfs::process()
{
}

void Dfs::resolveMsg(const Messages::DfsMessage &msg)
{
    qDebug() << msg.serialize();
}

void Dfs::appendSubscribtion(const BigNumber &actorId)
{
    Subscribtion sub;
    sub.add(actorId);
}

void Dfs::statusResponse()
{
    QDir dir(based_dfs_struct::ROOT_FOOLDER_NAME);
    QStringList actorsList = dir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
    QList<QString> list;
    for (const QString &el : actorsList)
    {
        if (el != based_dfs_struct::ROOT_CARD_FILE_NAME)
            list << CardManager::getAllFiles(BigNumber(el.toUtf8()));
    }
    //    for (const QString &el : list)
}
