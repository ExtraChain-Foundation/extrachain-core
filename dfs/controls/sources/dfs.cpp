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
    QDir().mkdir(based_dfs_struct::ROOT_FOOLDER_NAME);
    QDir().mkdir(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId);
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
    for (QByteArray currentPath : subPathList)
        QDir().mkpath(based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId + currentPath);

    qDebug() << "[init dfs for user]" << userId;
    //    signalConnections();

    QFile(based_dfs_struct::ACTOR_CARD_FILE).open(QIODevice::WriteOnly | QIODevice::Truncate);
    qDebug() << "[init finished]";
}

void Dfs::saveD(const QString &path, const based_dfs_struct::Type &type,
                const based_dfs_struct::SubType &subType, const based_dfs_struct::Status &status)
{
    QByteArray dfsSubPath;
    if (type == based_dfs_struct::Type::images)
        dfsSubPath = "/images/";
    else if (type == based_dfs_struct::Type::ivideo)
        dfsSubPath = "/video/";
    else if (type == based_dfs_struct::Type::events)
        dfsSubPath = "/events/";
    else if (type == based_dfs_struct::Type::system)
        dfsSubPath = "/system/";
    else if (type == based_dfs_struct::Type::chates)
        dfsSubPath = "/chats/";
    else if (type == based_dfs_struct::Type::postes)
        dfsSubPath = "/posts/";
    else if (type == based_dfs_struct::Type::servic)
        dfsSubPath = "/services/";
    else if (type == based_dfs_struct::Type::cdoctp)
        dfsSubPath = "/cdoctp/";
    else if (type == based_dfs_struct::Type::card)
        dfsSubPath = "/cards/";
    QFile file(path);
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();
    file.close();
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QByteArray name = setName(userId);
    BigNumber sectionIndex = getActualSection(type);
    BigNumber elementIndex = getActualElementInSection(sectionIndex, type);
    if (elementIndex >= BigNumber("99"))
    {
        sectionIndex++;
        createNewSection(sectionIndex, type);
        createNewElement(BigNumber("0"), sectionIndex, type);
        elementIndex = BigNumber("-1");
    }
    elementIndex++;
    createNewElement(elementIndex, sectionIndex, type);
    elementIndex = sectionIndex * 100 + elementIndex;

    QByteArray dfsPath = based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + dfsSubPath
        + sectionIndex.toByteArray() + '/' + elementIndex.toByteArray();
    appendC(dfsPath, userId, name, based_dfs_struct::toByteArray(type));
    QFile dfsFile(dfsPath);
    dfsFile.open(QIODevice::WriteOnly | QIODevice::Truncate);
    dfsFile.write(data);
    dfsFile.flush();
    dfsFile.close();
    //    emit sendQ(dfsPath, type, SocketPair());
    sender->sendFile(dfsPath, type, SocketPair());
#ifdef ETALONIUM_CLIENT
    emit usersChanges(dfsPath, type, userId); // TODO
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
    if (data1 == data2)
    {
        // Vsё ok
    }
    else
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

void Dfs::createNewSection(BigNumber sectionIndex, based_dfs_struct::Type sectionType)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QDir().mkpath(based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
                  + based_dfs_struct::toByteArray(sectionType) + '/' + sectionIndex.toByteArray());
    QFile file(based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
               + based_dfs_struct::toByteArray(sectionType) + "/section");
    if (file.open(QIODevice::WriteOnly))
        file.write(sectionIndex.toByteArray());
    else
        qDebug() << "[Error] dfs.cpp. Can't create section in createNewSection method.";
    file.close();
    createNewElement(BigNumber("-1"), sectionIndex, sectionType);
}

void Dfs::createNewElement(BigNumber elementIndex, BigNumber sectionIndex, based_dfs_struct::Type sectionType)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QDir().mkpath(based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
                  + based_dfs_struct::toByteArray(sectionType) + '/' + sectionIndex.toByteArray());
    QFile file(based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
               + based_dfs_struct::toByteArray(sectionType) + '/' + sectionIndex.toByteArray() + "/element");
    if (file.open(QIODevice::WriteOnly))
        file.write(elementIndex.toByteArray());
    else
        qDebug() << "[Error] dfs.cpp. Can't create element in createNewElement method.";
    file.close();
}

void Dfs::saveFN(const QString tmpPath, const QString &path, const based_dfs_struct::Type &type)
{
    QFile file(tmpPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "SaveFN not succeded: file not opened";
        return;
    }
    if (type == based_dfs_struct::Type::card)
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

    appendC(path, pathList.at(PathStruct::aId), pathList.at(PathStruct::name),
            based_dfs_struct::toByteArray(type));
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
            el.chop(based_dfs_struct::FILE_IDENTIFICATOR.size());
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
        based_dfs_struct::Type type = CardManager::getTypeByName(path, pathList.at(PathStruct::aId));
        if (pathList.at(PathStruct::name) == based_dfs_struct::ACTOR_CARD_FILE)
            type = based_dfs_struct::card;
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
        QDir acDir(based_dfs_struct::ROOT_FOOLDER_NAME);
        QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
        for (const QString &el : acList)
        {
            QStringList fList = CardManager::getAllFiles(el.toUtf8());
            for (const QString &file : fList)
            {
                based_dfs_struct::Type ftype = CardManager::getTypeByName(file, el.toUtf8());
                sender->sendFile(file, ftype, receiver);
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
BigNumber Dfs::getActualSection(based_dfs_struct::Type type)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QFile file(based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
               + based_dfs_struct::toByteArray(type) + "/section");
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

BigNumber Dfs::getActualElementInSection(BigNumber sectiondIndex, based_dfs_struct::Type sectionType)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QFile file(based_dfs_struct::ROOT_FOOLDER_NAME.toUtf8() + '/' + userId + '/'
               + based_dfs_struct::toByteArray(sectionType) + '/' + sectiondIndex.toByteArray() + "/element");
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

void Dfs::savedNewData(const QString &path, const based_dfs_struct::Type &type,
                       const based_dfs_struct::SubType &subType, const based_dfs_struct::Status &status)
{
#ifdef ETALONIUM_CLIENT
    if (type == based_dfs_struct::Type::images && subType != based_dfs_struct::SubType::mini)
    {
        QImage im(path);
        if (im.save("temp", "jpeg", 70))
        {
            saveD("temp", type, subType, status);
            QFile filed("temp");
            filed.remove();
            return;
        }
    }
#endif
    saveD(path, type, subType, status);
}

void Dfs::process()
{
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

    statusD();
    initD(userId);
    QDir acDir(based_dfs_struct::ROOT_FOOLDER_NAME);
    if (acDir.exists())
    {
        QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
        for (const QString &el : acList)
        {
            if (el.toUtf8() != (*accountControler->getMainActor()).getId().toActorId())
            {
                QString cPath =
                    based_dfs_struct::ROOT_FOOLDER_NAME + '/' + el + '/' + based_dfs_struct::ACTOR_CARD_FILE;
                DFSMessage::dfs_request rqst(cPath, accountControler->getMainActor()->getId().toActorId());
                dfsNetManager->send(rqst.serialize());
            }
        }
    }
}

void Dfs::initUser(BigNumber userId)
{
    initD(userId.toActorId());
    QString cPath = based_dfs_struct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/'
        + based_dfs_struct::ACTOR_CARD_FILE;
    if (accountControler->getMainActor() == nullptr)
        return;
    DFSMessage::dfs_request rqst(cPath, accountControler->getMainActor()->getId().toActorId());
    dfsNetManager->send(rqst.serialize());
}
