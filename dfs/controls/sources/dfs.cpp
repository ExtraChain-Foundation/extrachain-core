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

void Dfs::initUserCards()
{
    DBConnector dbc((KeyStore::KEYSTORE + "/" + ".uc").toStdString());
    dbc.createTable(Config::DataStorage::userCardTable);
    dbc.close();
}

void Dfs::initDFS(const QByteArray &userId)
{
    QDir().mkdir(dfsStruct::ROOT_FOOLDER_NAME);
    QDir().mkdir(dfsStruct::ROOT_FOOLDER_NAME + '/' + userId);
    QList<QByteArray> subPathList;
    subPathList.append("/images/");
    subPathList.append("/video/");
    subPathList.append("/events/");
    subPathList.append("/system/");
    subPathList.append("/chats/");
    subPathList.append("/posts/");
    subPathList.append("/services/");
    subPathList.append("/cdoctp/");
    subPathList.append("/cards/");
    DBConnector dbc(
        (dfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/" + dfsStruct::ACTOR_CARD_FILE).toStdString());
    dbc.createTable(Config::DataStorage::cardTable);
    dbc.createTable(Config::DataStorage::lastSectionTable);
    for (int i = 0; i <= dfsStruct::Type::card; i++)
    {
        DBRow row;
        row.insert(std::to_string(i), std::to_string(0));
        dbc.insert(Config::DataStorage::lsTableName, row);
    }
    dbc.close();
    for (QByteArray currentPath : subPathList)
        QDir().mkpath(dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + currentPath);

    qDebug() << "[init dfs for user]" << userId;
    //    signalConnections();
    qDebug() << "[init finished]";
}

void Dfs::saveToDFS(const QString &path, const dfsStruct::Type &type, const dfsStruct::SubType &subType,
                    const dfsStruct::Status &status)
{
    QFile file(path);
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QByteArray dfsPath = buildDfsPath(userId, type);
    //
    appendToCard(dfsPath, userId, type, subType);
    if (!file.copy(dfsPath))
    {
        QFile::remove(dfsPath);
        file.copy(dfsPath);
    }
    sender->sendFile(dfsPath, type, SocketPair());
#ifdef ETALONIUM_CLIENT
    // emit usersChanges(dfsPath, type, userId); // TODO
#endif
}

void Dfs::appendToCard(const QString &path, const QByteArray &userId, const dfsStruct::Type &type,
                       const dfsStruct::SubType &subType)
{
    DBConnector dbc(
        (dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/' + dfsStruct::ACTOR_CARD_FILE).toStdString());
    DBRow row;
    row.insert(std::string("path"), path.toStdString());
    row.insert(std::string("date"), std::to_string(QDateTime::currentDateTime().toSecsSinceEpoch()));
    row.insert(std::string("type"), std::to_string(type));
    row.insert(std::string("subtype"), std::to_string(subType));
    row.insert(std::string("hash"), std::string(""));
    dbc.insert(Config::DataStorage::cardTableName, row);
}

QStringList Dfs::returnDifs(const QString &adin, const QString &dva)
{
    QFile file1(adin);
    QFile file2(dva);
    if (!file1.exists())
    {
        qDebug() << "first file is not exist";
        return {};
    }
    file1.open(QIODevice::ReadOnly);
    QByteArray data1 = file1.readAll();
    file1.flush();
    file1.close();
    QStringList result;
    if (!file2.exists())
    {
        QByteArrayList d1 = Serialization::deserialize(data1, Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
        qDebug() << "second file is not exist";
        for (const QByteArray &el : d1)
        {
            result.append(el);
        }
        return result;
    }

    file2.open(QIODevice::ReadOnly);
    QByteArray data2 = file2.readAll();
    file2.flush();
    file2.close();
    if (data1 != data2)
    {
        QByteArrayList d1 = Serialization::deserialize(data1, Serialization::DFS_ROOT_CARD_FILE_DELIMITER);
        QByteArrayList d2 = Serialization::deserialize(data2, Serialization::DFS_ROOT_CARD_FILE_DELIMITER);

        for (const QByteArray &el : d1)
        {
            if (!d2.contains(el))
            {
                result.append(el);
            }
        }
        for (const QByteArray &el : d2)
        {
            if (!d1.contains(el))
            {

                result.append(el);
            }
        }
    }
    return result;
}

void Dfs::getDFSStatus()
{
    if (QDir(dfsStruct::ROOT_FOOLDER_NAME).exists())
    {
        QDir dir(dfsStruct::ROOT_FOOLDER_NAME);
        QStringList list = dir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
        for (const QString &el : list)
        {
            if (el != dfsStruct::ACTOR_CARD_FILE)
            {
                DFSMessage::Status status(el.toUtf8(), CardManager::getAllFiles(el.toUtf8()));
                emit sendMsg(status.serialize(), Messages::DFS_MESSAGE, SocketPair());
            }
        }
    }
    else
    {
        DFSMessage::Status status("1", QStringList());
        emit sendMsg(status.serialize(), Messages::DFS_MESSAGE, SocketPair());
    }
}

void Dfs::signalConnection()
{
    //    connect(sender, &Sender::sendPckg, dfsNetManager, &DFSNetManager::send);
    //    connect(this, &Dfs::sendQ, sender, &Sender::sendFile);
    //    connect(resolver, &DFSResolver::save, this, &Dfs::saveFN);
    //    connect(this, &Dfs::resolveMsg, resolver, &DFSResolver::receiveMsg);
    //    connect(resolver, &DFSResolver::checkStatus, this, &Dfs::checkAc);
    //    connect(resolver, &DFSResolver::closingMsg, sender, &Sender::checkClosing);
    //    connect(resolver, &DFSResolver::initDfs, this, &Dfs::initUser);
}

void Dfs::createNewSection(BigNumber sectionIndex, dfsStruct::Type sectionType)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QDir().mkpath(dfsStruct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
                  + dfsStruct::toByteArray(sectionType) + '/' + sectionIndex.toByteArray());
    QFile file(dfsStruct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
               + dfsStruct::toByteArray(sectionType) + "/section");
    if (file.open(QIODevice::WriteOnly))
        file.write(sectionIndex.toByteArray());
    else
        qDebug() << "[Error] dfs.cpp. Can't create section in createNewSection method.";
    file.close();
    createNewElement(BigNumber("-1"), sectionIndex, sectionType);
}

void Dfs::createNewElement(BigNumber elementIndex, BigNumber sectionIndex, dfsStruct::Type sectionType)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QDir().mkpath(dfsStruct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
                  + dfsStruct::toByteArray(sectionType) + '/' + sectionIndex.toByteArray());
    QFile file(dfsStruct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
               + dfsStruct::toByteArray(sectionType) + '/' + sectionIndex.toByteArray() + "/element");
    if (file.open(QIODevice::WriteOnly))
        file.write(elementIndex.toByteArray());
    else
        qDebug() << "[Error] dfs.cpp. Can't create element in createNewElement method.";
    file.close();
}

void Dfs::saveFN(const QString tmpPath, const QString &path, const dfsStruct::Type &type)
{
    QFile file(tmpPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "SaveFN not succeded: file not opened";
        return;
    }
    if (type == dfsStruct::Type::card)
    {
        QStringList difs = returnDifs(tmpPath, path);
        for (const QString &el : difs)
        {
            QByteArrayList res =
                Serialization::deserialize(el.toUtf8(), Serialization::DFS_CARD_FILE_SECTION_DELIMETR);
            DFSMessage::dfs_request rqst(res.at(0), (*accountControler->getMainActor()).getId().toActorId());
            dfsNetManager->send(rqst.serialize());
        }
        file.remove();
        return;
    }
    file.close();
    file.rename(path);

    QList<QByteArray> pathList = Serialization::deserialize(path.toUtf8() + '/', "/");

    appendToCard(path, pathList.at(PathStruct::aId), pathList.at(PathStruct::name),
                 dfsStruct::toByteArray(type));
    QByteArray prevFile = pathList.at(PathStruct::name);
    BigNumber prFB = BigNumber(prevFile);
    prFB--;
    QByteArray prevFilePath =
        Serialization::serialize({ pathList.at(PathStruct::rFolder), pathList.at(PathStruct::aId) }, "/")
        + prFB.toByteArray();
    QDir dir(pathList.at(PathStruct::rFolder) + '/' + pathList.at(PathStruct::aId));
    QStringList list = dir.entryList({ "*.tmp" }, QDir::Files | QDir::NoDotAndDotDot);
    if (!list.isEmpty())
    {
        for (QString el : list)
        {
            el.chop(dfsStruct::FILE_IDENTIFICATOR.size());
            QString path = pathList.at(PathStruct::rFolder) + '/' + pathList.at(PathStruct::aId) + '/' + el;

            DFSMessage::dfs_request rqst(path, (*accountControler->getMainActor()).getId().toActorId());
            //            dfsNetManager->send(rqst.serialize());
            //            QFile(path + based_dfs_struct::FILE_IDENTIFICATOR).remove();
        }
    }
    //    if (QFile(prevFilePath + based_dfs_struct::FILE_IDENTIFICATOR.toUtf8()).exists())
    //    {
    //        Message::dfs_request rqst(prevFilePath,
    //        accountControler->getCurrentActor().getId().toActorId()); dfsNetManager->send(rqst.serialize());
    //    }
    sender->sendFile(path, type, SocketPair());
#ifdef ETALONIUM_CLIENT
    emit usersChanges(path.toUtf8(), type, pathList.at(PathStruct::aId)); // TODO
#endif
}

void Dfs::fileResponse(const QString path, const SocketPair &receiver)
{
    QFile file(path);
    QByteArrayList pathList = Serialization::deserialize(path.toUtf8() + "/", "/");
    if (file.exists())
    {
        dfsStruct::Type type = CardManager::getTypeByName(path, pathList.at(PathStruct::aId));
        if (pathList.at(PathStruct::name) == dfsStruct::ACTOR_CARD_FILE)
            type = dfsStruct::card;
        sender->sendFile(path, type, receiver);
    }
    return;
}

void Dfs::resendFragments(QString path, QList<QByteArray> frags)
{
    sender->resendFragments(
        path, CardManager::getTypeByName(path, Serialization::deserialize(path, '/').at(1).toUtf8()), frags);
}

void Dfs::checkAc(const QByteArray &actorId, const QStringList &request, const SocketPair &receiver)
{
    qDebug() << "[&Dfs] check dfs for " << actorId;
    if (actorId == "1")
    {
        QDir acDir(dfsStruct::ROOT_FOOLDER_NAME);
        QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
        for (const QString &el : acList)
        {
            QStringList fList = CardManager::getAllFiles(el.toUtf8());
            for (const QString &file : fList)
            {
                dfsStruct::Type ftype = CardManager::getTypeByName(file, el.toUtf8());
                sender->sendFile(file, ftype, receiver);
            }
        }
    }
    QDir dir(dfsStruct::ROOT_FOOLDER_NAME + '/' + actorId);
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
                dfsStruct::Type type = CardManager::getTypeByName(el, actorId);
                if (type != dfsStruct::service)
                    sender->sendFile(el, type, receiver);
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
}

Dfs::~Dfs()
{
}
BigNumber Dfs::getActualSection(dfsStruct::Type type)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QFile file(dfsStruct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/' + dfsStruct::toByteArray(type)
               + "/section");
    if (!file.exists())
    {
        createNewSection(BigNumber("0"), type);
        return BigNumber("0");
    }
    QByteArray sectionIndex = "0";
    if (file.open(QIODevice::ReadOnly))
        sectionIndex = file.readLine();
    file.close();
    return BigNumber(sectionIndex);
}

BigNumber Dfs::getActualElementInSection(BigNumber sectiondIndex, dfsStruct::Type sectionType)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QFile file(dfsStruct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
               + dfsStruct::toByteArray(sectionType) + '/' + sectiondIndex.toByteArray() + "/element");
    if (file.exists())
    {
        QByteArray elementIndex = "0";
        if (file.open(QIODevice::ReadOnly))
            elementIndex = file.readLine();
        file.close();
        return BigNumber(elementIndex);
    }
    qDebug() << "[Error] Error in getActualElementInSection. File with element non exist, WHYYYYY??!?!?";
    return BigNumber("NULL");
}

void Dfs::initDFSNetManager(ResolveManager *resolveManager)
{
    dfsNetManager = new DFSNetManager(accountControler, actorIndex);
    dfsNetManager->setResolveManager(resolveManager);
    ThreadPool::addThread(dfsNetManager);
}

void Dfs::savedNewData(const QString &path, const dfsStruct::Type &type, const dfsStruct::SubType &subType,
                       const dfsStruct::Status &status)
{
#ifdef ETALONIUM_CLIENT
    if (type == dfsStruct::Type::images && subType != dfsStruct::SubType::mini)
    {
        QImage im(path);
        if (im.save("temp", "jpeg", 80))
        {
            saveToDFS("temp", type, subType, status);
            //            QFile filed("temp");
            //            filed.remove();
            return;
        }
    }
#endif
    saveToDFS(path, type, subType, status);
}

void Dfs::process()
{
}

QByteArray Dfs::buildDfsPath(QByteArray userID, dfsStruct::Type type)
{
    QByteArray sType = dfsStruct::toByteArray(type);
    QByteArray dfsPath = "data/" + userID + "/" + sType + "/";
    BigNumber ss = BigNumber(Config::DataStorage::SECTION_SIZE);
    DBConnector dfsCard(("data/" + userID + dfsStruct::ACTOR_CARD_FILE).toStdString());
    std::vector<DBRow> res = dfsCard.select(
        ("SELECT last_section FROM Section WHERE type='" + QByteArray::number(type) + "';").toStdString());
    if (!res.empty())
    {
        BigNumber lsmax(QByteArray::fromStdString(res[0]["last_section"]));
        if (ss - lsmax <= 0)
        {
            lsmax++;
            bool updres = dfsCard.update(("UPDATE Section SET last_section='" + lsmax.toByteArray()
                                          + "' WHERE type=" + QByteArray::number(type) + ";")
                                             .toStdString());
            if (!updres)
            {
                qDebug() << "path creation in UPDATE section failed";
                return QByteArray();
            }
            else
            {
                dfsPath.append(lsmax.toByteArray());
                QDir dir;
                dir.mkpath(dfsPath);
            }
        }
        else
        {
            dfsPath.append(lsmax.toByteArray());
        }
    }
    else
    {
        qDebug() << "DB Section corrupted";
        return QByteArray();
    }
    QDir dir(dfsPath);
    //        dir.entryList(QDir:: | QDir::NoDotAndDotDot).last();
    QByteArray n = QByteArray::number(dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), 16);
    return dfsPath + "/" + n;
}

void Dfs::init()
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    sender = new Sender(userId);
    sender->setNetManager(dfsNetManager);
    //    resolver = new DFSResolver(actorIndex);
    //
    signalConnection();
    ThreadPool::addThread(sender);
    //    ThreadPool::addThread(resolver);

    getDFSStatus();
    initDFS(userId);
    QDir acDir(dfsStruct::ROOT_FOOLDER_NAME);
    if (acDir.exists())
    {
        QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
        for (const QString &el : acList)
        {
            if (el.toUtf8() != (*accountControler->getMainActor()).getId().toActorId())
            {
                QString cPath = dfsStruct::ROOT_FOOLDER_NAME + '/' + el + '/' + dfsStruct::ACTOR_CARD_FILE;
                DFSMessage::dfs_request rqst(cPath, accountControler->getMainActor()->getId().toActorId());
                dfsNetManager->send(rqst.serialize());
            }
        }
    }
}

void Dfs::initUser(BigNumber userId)
{
    initDFS(userId.toActorId());
    QString cPath =
        dfsStruct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/' + dfsStruct::ACTOR_CARD_FILE;
    if (accountControler->getMainActor() == nullptr)
        return;
    DFSMessage::dfs_request rqst(cPath, accountControler->getMainActor()->getId().toActorId());
    dfsNetManager->send(rqst.serialize());
}
