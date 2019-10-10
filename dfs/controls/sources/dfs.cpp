#include "dfs/controls/headers/dfs.h"
#include <iterator>
void Dfs::signalConnections()
{
    qDebug() << connect(dfsIndex, &DfsIndex::sendData, this, &Dfs::sendMessage);
    qDebug() << "dfs request connection" << connect(dfsIndex, &DfsIndex::sendToUser, this, &Dfs::sendToPeer);
    connect(dfsIndex, &DfsIndex::sendRequest, this, &Dfs::sendRequestf);
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
    QString createdPath = based_dfs_struct::ROOT_FOOLDER_NAME + '/'
        + accountControler->getMainActor()->getId().toByteArray() + '/' + based_dfs_struct::toString(type)
        + '/';
    createdPath += based_dfs_struct::images == type ? based_dfs_struct::toString(subType) + '/' : "";
    if (type != based_dfs_struct::servic)
        createdPath += CardManager::getNameForNewFile(type).toByteArray();
    else
    {
        if (subType == based_dfs_struct::profil)
            createdPath += "profile.dat";
        else
            createdPath += "avatar";
    }
    createdPath += '&' + path;
    dfsIndex->changedData(createdPath, type, subType, status);

#ifdef ETALONIUM_CLIENT
    emit usersChanges(path.toUtf8(), based_dfs_struct::Type::system, BigNumber("-2")); // TODO
#endif
}

// void Dfs::downloadRecieve(Messages::DownloadDfsRequestData msg, QString sender)
//{
//    if (msg.getStatus())
//    {
//        QString filePath = msg.getHeader();
//        QFile file(msg.getHeader());
//        file.open(QIODevice::ReadOnly);
//        QByteArray data = file.readAll();
//        int size = data.size();
//        if (size < 1025)
//        {
//            Messages::DfsMessage msg(data, size, filePath);
//            QByteArray data = msg.serialize();
//            emit sendToPeer(msg, sender);
//        }
//        else
//        {
//            QList<Messages::DfsMessage> list = {};
//            int step = 1024;
//            QByteArray dataRaw = Serialization::desirializeStored(data).at(1);
//            for (int i = 0; i < dataRaw.size(); i += step)
//            {
//                if ((i + step) < dataRaw.size())
//                    list.append(Messages::DfsMessage(dataRaw.mid(i, step), step,
//                    filePath));
//                else
//                    list.append(Messages::DfsMessage(data.mid(i, dataRaw.size() - i),
//                                                     dataRaw.size() - i, filePath));
//            }
//            QList<QByteArray> reslutList = {};
//            for (Messages::DfsMessage msg : list)
//                emit sendToPeer(msg, sender);
//        }
//    }
//}

void Dfs::downloadRequset(QByteArray header, QString peerAddress)
{
    QList<QByteArray> list = Serialization::deserialize(header, Serialization::DFS_STORED_DELIMETR);
    if (!QFile(list.at(3)).exists())
        emit downloadResponse(true, header, peerAddress);
}
void Dfs::init()
{
    dfsIndex = new DfsIndex(actorIndex, accountControler);
    signalConnections();
    BigNumber id = accountControler->getMainActor() == nullptr ? BigNumber(-1)
                                                               : accountControler->getMainActor()->getId();
    CardManager::createdCardFilesConnection(id);
    int error = CardManager::checkDfsState(id);
    if (error == -1)
    {
        dfsIndex->makeSystemDir(accountControler->getMainActor()->getId());
    }
    else
    {
    }
    qDebug() << "[Dfs]:: dfs has been init";
    emit beginTest();
    Subscribtion sub;
    QList<BigNumber> listUsers = sub.getAll(accountControler->getMainActor()->getId());
    for (const BigNumber &i : listUsers)
        if (actorIndex->getActor(i).getAccount() && (i != accountControler->getMainActor()->getId()))
            listUsers.append(i);
    std::for_each(listUsers.begin(), listUsers.end(), [this](BigNumber userId) {
        Messages::DfsRequest rqst(DFS_REQUESTS::DFS_ALL, userId.toByteArray());
        emit sendRequestf(rqst);
    });
}

void Dfs::initUser(BigNumber userId)
{
    dfsIndex->makeSystemDir(userId);
    CardManager::createdAllCards(userId);
    Messages::DfsRequest rqst(DFS_REQUESTS::DFS_ALL, userId.toByteArray());
    emit sendRequestf(rqst);
    qDebug() << "init start dfs for user - " << userId.toByteArray();
}

void Dfs::recieveRequest(Messages::DfsRequest request, QString peerAdress)
{
    switch (request.getRequest())
    {
    case DFS_REQUESTS::DFS_ALL:
    {
        QList<QString> list;
        QList<BigNumber> actorList;
        list << CardManager::getAllFiles(
            BigNumber(request.getFilePath().toUtf8()) /*BigNumber(request.getFilePath().toUtf8())*/);
        for (QString &el : list)
            dfsIndex->dfsSender(el, peerAdress);
        return;
    }
    case DFS_REQUESTS::FILE_REQUEST:
    {
        if (QFile(request.getFilePath()).exists())
            dfsIndex->dfsSender(request.getFilePath(), "");
    }
    }
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
    std::for_each(based_dfs_struct::typesVec.end(), based_dfs_struct::typesVec.end(),
                  [&isCardFile, &pathList](based_dfs_struct::Type el) {
                      QString r = based_dfs_struct::typeCardFilesMap.find(el)->second;
                      if (r == pathList.last())
                          isCardFile = true;
                  });

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
            std::for_each(requestList.begin(), requestList.end(), [this](QString el) {
                Messages::DfsRequest request(DFS_REQUESTS::FILE_REQUEST, el);
                emit sendRequestf(request);
            });
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
            if (filesQueue.find(Utils::calcKeccak(fileName.toUtf8())) != filesQueue.end())
            {
                QFile::rename(temp_History + Utils::calcKeccak(fileName.toUtf8()), fileName);
            }
            DfsItem dfsItem(fileName, based_dfs_struct::Status::REPLACE, actorIndex, accountControler,
                            msg.getData());
            CardManager::appendToCard(dfsItem.getType(), dfsItem.serialize(), dfsItem.getActorId());
            emit usersChanges(dfsItem.getPath(), dfsItem.getType(), dfsItem.getActorId());
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

void Dfs::getDfsRequest(const Messages::DfsRequest &msg)
{
    qDebug() << msg.serialize();
}

void Dfs::checkStatus(const Messages::DfsStatus &msg)
{
    std::vector<std::pair<std::string, std::string>> localFileList =
        CardManager::getAllFileWithHash(msg.getActorId());
    QStringList list = CardManager::getAllNotEmptyCardFile(msg.getActorId());
    if (localFileList != msg.getList())
        for (QString el : list)
            dfsIndex->dfsSender(el, "");
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
