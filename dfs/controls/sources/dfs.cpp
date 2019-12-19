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
    dbc.createTable(Config::DataStorage::cardTableCreation);
    dbc.createTable(Config::DataStorage::lastSectionTableCreation);
    for (int i = 0; i <= dfsStruct::Type::card; i++)
    {
        DBRow row;
        row.insert({ "counter", "-1" });
        row.insert({ "type", std::to_string(i) });
        dbc.insert(Config::DataStorage::lsTableName, row);
    }
    for (QByteArray currentPath : subPathList)
        QDir().mkpath(dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + currentPath);

    qDebug() << "[init dfs for user]" << userId;
    //    signalConnections();
    qDebug() << "[init finished]";
}

void Dfs::saveToDFS(const QString &path, const QByteArray &data, const dfsStruct::Type &type,
                    const dfsStruct::SubType &subType)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QByteArray dfsPath = buildDfsPath(userId, type);
    bool stored = false;

    if (!appendToCard(dfsPath, userId, type, subType))
        return;

    if (type == dfsStruct::post || type == dfsStruct::event || type == dfsStruct::service
        || type == dfsStruct::contract || type == dfsStruct::chat)
    {
        if (!createStored(dfsPath, userId, type))
        {
            return;
        }
        else
        {
            stored = true;

            // temp
            QString range = QString("0:%1").arg(data.size());
            DFSMessage::DfsChanges dfsChanges(dfsPath, { data }, range, 3, userId, userId);

            appendToStored(dfsPath, data, range, 3, userId, true);
        }
    }

    if (path.isEmpty()) // if !path AND data
    {
        QFile file(dfsPath);
        file.open(QFile::WriteOnly);
        file.write(data);
        file.close();
    }
    else // if path
    {
        QFile file(path);
        if (!file.copy(dfsPath))
        {
            QFile::remove(dfsPath);
            file.copy(dfsPath);
        }
    }

    if (stored)
        sender->sendFile(dfsPath + dfsStruct::STORED_FILE_NAME, type, SocketPair());
    sender->sendFile(dfsPath, type, SocketPair());
#ifdef ETALONIUM_CLIENT
    emit usersChanges(dfsPath, type, userId); // TODO
#endif
}

bool Dfs::appendToCard(const QString &path, const QByteArray &userId, const dfsStruct::Type &type,
                       const dfsStruct::SubType &subType)
{
    DBConnector dbc(
        (dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/' + dfsStruct::ACTOR_CARD_FILE).toStdString());
    DBRow row;
    row.insert({ "path", path.toStdString() });
    row.insert({ "date", std::to_string(QDateTime::currentDateTime().toSecsSinceEpoch()) });
    row.insert({ "type", std::to_string(type) });
    row.insert({ "subtype", std::to_string(subType) });
    row.insert({ "hash", "" });
    return dbc.insert(Config::DataStorage::cardTableName, row);
}

QStringList Dfs::returnDiffs(const QString &odin, const QString &odinson) //
{
    QFile file1(odin);
    QFile file2(odinson);
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
        QStringList diffs = returnDiffs(tmpPath, path);
        //        for (const QString &el : difs)
        //        {
        //            QByteArrayList res =
        //                Serialization::deserialize(el.toUtf8(),
        //                Serialization::DFS_CARD_FILE_SECTION_DELIMETR);
        //            DFSMessage::dfs_request rqst(res.at(0),
        //            (*accountControler->getMainActor()).getId().toActorId());
        //            dfsNetManager->send(rqst.serialize());
        //        }
        //        file.remove();
        return;
    }
    file.close();
    file.rename(path);

    QList<QByteArray> pathList = Serialization::deserialize(path.toUtf8() + '/', "/");

    appendToCard(path, pathList.at(PathStruct::aId), type);
    sender->sendFile(path, type, SocketPair());
#ifdef ETALONIUM_CLIENT
    emit usersChanges(path.toUtf8(), type, pathList.at(PathStruct::aId)); // TODO
#endif
}

void Dfs::fileResponse(const QString path, const SocketPair &receiver)
{
    QFile file(path);
    QByteArrayList pathList = Serialization::deserialize(path.toUtf8() + "/", "/");

    //    return;

    if (file.exists())
    {
        dfsStruct::Type type = CardManager::getTypeByName(path, pathList.at(PathStruct::aId));
        //        if (pathList.at(PathStruct::name) == dfsStruct::ACTOR_CARD_FILE)
        //            type = dfsStruct::card;
        sender->sendFile(path, type, receiver);
    }
    return;
}

void Dfs::sendFragments(QString path, QByteArray frags, SocketPair receiver)
{
    sender->sendFragments(
        path, CardManager::getTypeByName(path, Serialization::deserialize(path, '/').at(1).toUtf8()), frags,
        receiver);
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
    connect(this, &Dfs::sendFromNetwork, this, &Dfs::save);
}

Dfs::~Dfs()
{
}

void Dfs::initDFSNetManager(ResolveManager *resolveManager)
{
    dfsNetManager = new DFSNetManager(accountControler, actorIndex);
    dfsNetManager->setResolveManager(resolveManager);
    dfsNetManager->setDfs(this);
    ThreadPool::addThread(dfsNetManager);
}

void Dfs::saveStaticFile(QString fileName, dfsStruct::Type type, dfsStruct::SubType subType)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QByteArray sType = dfsStruct::toByteArray(type);
    QString dfsPath = "data/" + userId + "/" + sType + "/" + fileName;

    if (!QFile::exists(dfsPath)) // and no stored
        return;

    bool stored = false;
    if (type == dfsStruct::post || type == dfsStruct::event || type == dfsStruct::service
        || type == dfsStruct::contract || type == dfsStruct::chat)
    {
        if (!createStored(dfsPath, userId, type))
        {
            return;
        }
        else
        {
            stored = true;

            QFile file(dfsPath);
            file.open(QFile::ReadOnly);
            QByteArray data = file.readAll();
            file.close();

            // temp
            QString range = QString("0:%1").arg(data.size());
            DFSMessage::DfsChanges dfsChanges(dfsPath, { data }, range, 3, userId, userId);
            bool card = appendToCard(dfsPath, userId, type, subType);
            bool stored = appendToStored(dfsPath, data, range, 3, userId, true);

            if (!card)
                return;
            if (!stored)
                return;
        }
    }

    sender->sendFile(dfsPath + dfsStruct::STORED_FILE_NAME, type, SocketPair());
    sender->sendFile(dfsPath, type, SocketPair());

#ifdef ETALONIUM_CLIENT
    emit usersChanges(dfsPath.toLatin1(), type, userId);
#endif
}

void Dfs::editData(QString userId, QString fileName, dfsStruct::Type type, QByteArray data)
{
    DFSMessage::DfsChanges dfsChanges;
    dfsChanges.userId = userId.toLatin1();
    dfsChanges.changeType = 3;
    dfsChanges.signature = accountControler->getMainActor()->getKey()->encrypt(dfsChanges.userId);
    int pckg = 0;

    QByteArray sType = dfsStruct::toByteArray(type);
    dfsChanges.filePath = "data/" + dfsChanges.userId + "/" + sType + "/" + fileName;
    QFile file(dfsChanges.filePath);
    qDebug() << "->" << file.open(QFile::ReadOnly);

    QByteArrayList pckgNums;

    while (file.bytesAvailable() > 0)
    {
        auto readed = file.read(DFSMessage::dataSize);

        QByteArray newDataPart = data.mid(DFSMessage::dataSize * pckg, DFSMessage::dataSize);
        qDebug() << "rea" << readed;
        qDebug() << "new" << newDataPart;
        qDebug() << "";
        if (readed != newDataPart)
        {
            pckgNums << QByteArray::number(pckg);
            //            dfsChanges.range += " " + QByteArray::number(pckg);
            dfsChanges.data << newDataPart;
        }

        pckg++;
    }

    file.close();

    if (data.size() > DFSMessage::dataSize * pckg)
    {
        int totalPckg = (data.size() - DFSMessage::dataSize * pckg) / DFSMessage::dataSize + pckg;

        for (int i = pckg; i <= totalPckg; ++i)
        {
            pckgNums << QByteArray::number(i);
            dfsChanges.data << data.mid(DFSMessage::dataSize * i, DFSMessage::dataSize);
        }
    }

    dfsChanges.range = pckgNums.join(" ");
    pckgNums.clear();

    qDebug() << dfsChanges.range;
    qDebug() << dfsChanges.data;

    if (applyChanges(dfsChanges))
        sender->sendDfsMessage(dfsChanges);
}

void Dfs::editSqlDatabase(QString userId, QString fileName, dfsStruct::Type type, int sqlType,
                          QByteArrayList sqlChanges)
{
    DFSMessage::DfsChanges dfsChanges;
    dfsChanges.data << sqlChanges;
    dfsChanges.range = "sql";
    dfsChanges.userId = userId.toLatin1();
    QByteArray sType = dfsStruct::toByteArray(type);
    dfsChanges.filePath = "data/" + dfsChanges.userId + "/" + sType + "/" + fileName;
    dfsChanges.signature = accountControler->getMainActor()->getKey()->encrypt(dfsChanges.userId);
    dfsChanges.changeType = sqlType;

    if (applyChanges(dfsChanges))
    {
        sender->sendDfsMessage(dfsChanges);
    }
}

bool Dfs::applyChanges(const DFSMessage::DfsChanges &dfsChanges)
{
    int type = dfsChanges.changeType;
    bool apply = false;

    if (type == dfsStruct::Bytes)
        apply = applyChangesBytes(dfsChanges);
    else if (type >= dfsStruct::Delete && type <= dfsStruct::Update)
        apply = applyChangesSql(dfsChanges);

    if (apply)
        return appendToStored(dfsChanges.filePath, Serialization::universalSerialize(dfsChanges.data, 8),
                              dfsChanges.range, dfsChanges.changeType, dfsChanges.userId);

    return false;
}

bool Dfs::applyChangesBytes(const DFSMessage::DfsChanges &dfsChanges)
{
    QString filePathCtmp = dfsChanges.filePath + ".ctmp";
    QFile file(dfsChanges.filePath);
    if (!file.open(QFile::ReadOnly))
    {
        qDebug() << "You cannot change what is not";
        return false;
    }
    QFile file3(filePathCtmp);

    file3.open(QFile::WriteOnly);
    QByteArrayList pckgNums = dfsChanges.range.split(' ');
    int max = pckgNums.length() ? pckgNums.last().toInt() : -1;

    for (int i = 0; i < max + 1; ++i)
    {
        int pos = DFSMessage::dataSize * i;

        int indexOf = pckgNums.indexOf(QByteArray::number(i));
        if (indexOf != -1)
        {
            file3.write(dfsChanges.data[indexOf]);
        }
        else
        {
            file.seek(pos);
            file3.write(file.read(DFSMessage::dataSize));
        }
    }

    file.close();
    file3.close();
    file.remove();

    if (file3.rename(dfsChanges.filePath))
    {
        return updateCard(dfsChanges.filePath, dfsChanges.userId,
                          QByteArray::number(QDateTime::currentDateTime().toSecsSinceEpoch()), 1);
    }

    return false;
}

bool Dfs::applyChangesSql(const DFSMessage::DfsChanges &dfsChanges)
{
    // TODO: escape sql & list size checks
    DBConnector db;
    db.open(dfsChanges.filePath.toStdString());
    QByteArrayList data = dfsChanges.data;

    if (dfsChanges.changeType == dfsStruct::Delete)
    {
        QByteArray query = "DELETE FROM " + data[0] + " WHERE " + data[1] + " = '" + data[2] + "'";
        // for (int i = 3; i != data.length(); i += 2)
        //    query += " AND " + data[1] + " = '" + data[2] + "'";
        return db.query(query.toStdString());
    }
    else if (dfsChanges.changeType == dfsStruct::Insert)
    {
        DBRow row;

        for (int i = 1; i < data.length(); i += 2)
        {
            row.insert({ data[i].toStdString(), data[i + 1].toStdString() });
        }

        return db.insert(data[0].toStdString(), row);
    }
    else if (dfsChanges.changeType == dfsStruct::Update)
    {
        // QByteArray query = "UPDATE " + data[0] + "SET ... WHERE " + data[1] + " = '" + data[2] + "';";
        return false; // db.update(query.toStdString());
    }

    return false;
}

void Dfs::process()
{
}

QByteArray Dfs::buildDfsPath(QByteArray userID, dfsStruct::Type type)
{
    QByteArray sType = dfsStruct::toByteArray(type);
    QByteArray dfsPath = "data/" + userID + "/" + sType + "/";
    BigNumber ss = BigNumber(Config::DataStorage::SECTION_SIZE);
    DBConnector dfsCard(("data/" + userID + "/" + dfsStruct::ACTOR_CARD_FILE).toStdString());
    QByteArray t = QByteArray::number(type);
    std::vector<DBRow> res =
        dfsCard.select(("SELECT counter FROM " + QByteArray(Config::DataStorage::lsTableName.c_str())
                        + " WHERE type='" + t + "';")
                           .toStdString());

    if (!res.empty())
    {
        BigNumber lsmax(QByteArray::fromStdString(res[0]["counter"]));
        lsmax++;
        BigNumber sec = lsmax / ss;
        bool updres = dfsCard.update(("UPDATE " + QByteArray(Config::DataStorage::lsTableName.c_str())
                                      + " SET counter='" + lsmax.toByteArray()
                                      + "' WHERE type=" + QByteArray::number(type) + ";")
                                         .toStdString());
        if (!updres)
        {
            qDebug() << "path creation in UPDATE section failed";
            return QByteArray();
        }
        else
        {
            dfsPath += sec.toByteArray() + "/";
            QDir dir;
            qDebug() << "mkpath result:" << dir.mkpath(dfsPath);
            dfsPath += lsmax.toByteArray();
            return dfsPath;
        }
    }
    else
    {
        qDebug() << "DB Section corrupted";
        return QByteArray();
    }
}

bool Dfs::createStored(QString filePath, const QByteArray &userId, const dfsStruct::Type &type)
{
    QString dfsPath = filePath + dfsStruct::STORED_FILE_NAME;
    DBConnector dbc;

    if (!dbc.open((filePath + dfsStruct::STORED_FILE_NAME).toStdString()))
        return false;
    if (!dbc.createTable(Config::DataStorage::storedTableCreation))
        return false;

    return appendToCard(dfsPath, userId, type, dfsStruct::SubType::stored);
}

bool Dfs::appendToStored(QString filePath, QByteArray data, QString range, int type, QString userId,
                         bool init)
{
    DBConnector dbc((filePath + dfsStruct::STORED_FILE_NAME).toStdString());
    QByteArray hash = Utils::calcKeccak(
        QByteArray::number(QRandomGenerator::global()->bounded(50000) + QDateTime::currentMSecsSinceEpoch()));
    QByteArray sign = accountControler->getMainActor()->getKey()->sign(userId.toLatin1());

    if (init)
    {
        DBRow row = { { "data", "" /*data.toStdString()*/ },
                      { "range", range.toStdString() },
                      { "type", std::to_string(type) },
                      { "uid", userId.toStdString() },
                      { "sign", sign.toStdString() },
                      { "hash", hash.toStdString() },
                      { "prevHash", "" } };

        return dbc.insert(Config::DataStorage::storedTableName, row);
    }

    QByteArray sep = "', '";
    QByteArray query = "INSERT INTO Stored ('hash', 'sign', 'type', 'uid', 'range', 'prevHash', "
                       "'data') SELECT '"
        + hash + sep + sign + sep + QByteArray::number(type) + sep + userId.toLatin1() + sep
        + range.toLatin1() + "', hash, '" + data.replace("'", "''")
        + "' FROM Stored ORDER BY key DESC LIMIT 1";

    return dbc.query(query.toStdString());
}

bool Dfs::updateCard(const QString &path, const QByteArray &userId, QByteArray date, int lastKey)
{
    DBConnector dbc;
    std::string rootPath =
        (dfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/' + dfsStruct::ACTOR_CARD_FILE).toStdString();

    if (!dbc.open(rootPath))
        return false;

    std::string query = QString("UPDATE %1 SET date = '%2' WHERE path = '%3';")
                            .arg(QString::fromStdString(Config::DataStorage::cardTableName))
                            .arg(QString::number(QDateTime::currentDateTime().toSecsSinceEpoch()))
                            .arg(path)
                            .toStdString();

    return dbc.update(query);
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
            //            if (el.toUtf8() != (*accountControler->getMainActor()).getId().toActorId())
            //            {
            //                QString cPath = dfsStruct::ROOT_FOOLDER_NAME + '/' + el + '/' +
            //                dfsStruct::ACTOR_CARD_FILE; DFSMessage::dfs_request rqst(cPath,
            //                accountControler->getMainActor()->getId().toActorId());
            //                dfsNetManager->send(rqst.serialize());
            //            }
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
    //    DFSMessage::dfs_request rqst(cPath, accountControler->getMainActor()->getId().toActorId());
    //    dfsNetManager->send(rqst.serialize());
}

void Dfs::save(int saveType, QString file, QByteArray data, const dfsStruct::Type type,
               const dfsStruct::SubType subType)
{
    switch (saveType)
    {
    case dfsStruct::DfsSave::File:
        saveToDFS(file, data, type, subType);
        break;
    case dfsStruct::DfsSave::Static:
        saveStaticFile(file, type, subType);
        break;
    case dfsStruct::DfsSave::Network:
        saveFN(file + dfsStruct::FILE_IDENTIFICATOR, file, type);
    }
}
