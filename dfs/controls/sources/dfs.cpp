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

Sender *Dfs::getSender() const
{
    return sender;
}

void Dfs::initDFS(const QByteArray &userId)
{
    QDir().mkdir(DfsStruct::ROOT_FOOLDER_NAME);
    QDir().mkdir(DfsStruct::ROOT_FOOLDER_NAME + '/' + userId);
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
        (DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/" + DfsStruct::ACTOR_CARD_FILE).toStdString());
    dbc.createTable(Config::DataStorage::cardTableCreation);
    dbc.createTable(Config::DataStorage::lastSectionTableCreation);
    for (int i = 0; i <= DfsStruct::Type::card; i++)
    {
        DBRow row;
        row.insert({ "counter", "-1" });
        row.insert({ "type", std::to_string(i) });
        dbc.insert(Config::DataStorage::lsTableName, row);
    }
    for (QByteArray currentPath : subPathList)
        QDir().mkpath(DfsStruct::ROOT_FOOLDER_NAME + '/' + userId + currentPath);

    qDebug() << "[init dfs for user]" << userId;
    //    signalConnections();
    qDebug() << "[init finished]";
}

void Dfs::saveToDFS(const QString &path, const QByteArray &data, const DfsStruct::Type &type,
                    const DfsStruct::SubType &subType)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QByteArray dfsPath = buildDfsPath(userId, type);
    bool stored = false;

    if (!appendToCard(dfsPath, userId, type, subType))
        return;

    if (type == DfsStruct::post || type == DfsStruct::event || type == DfsStruct::service
        || type == DfsStruct::contract || type == DfsStruct::chat)
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
        sender->sendFile(dfsPath + DfsStruct::STORED_FILE_NAME, type, SocketPair());
    sender->sendFile(dfsPath, type, SocketPair());
#ifdef ETALONIUM_CLIENT
    emit usersChanges(dfsPath, type, userId); // TODO
#endif
}

bool Dfs::appendToCard(const QString &path, const QByteArray &userId, const DfsStruct::Type &type,
                       const DfsStruct::SubType &subType)
{
    DBConnector dbc(
        (DfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/' + DfsStruct::ACTOR_CARD_FILE).toStdString());
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
    if (QDir(DfsStruct::ROOT_FOOLDER_NAME).exists())
    {
        QDir dir(DfsStruct::ROOT_FOOLDER_NAME);
        QStringList list = dir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
        for (const QString &el : list)
        {
            if (el != DfsStruct::ACTOR_CARD_FILE)
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

void Dfs::saveFN(const QString tmpPath, const QString &path, const DfsStruct::Type &type)
{
    QFile file(tmpPath);
    if (!file.open(QIODevice::ReadOnly))
    {
        qDebug() << "SaveFN not succeded: file not opened";
        return;
    }
    file.close();
    if (type == DfsStruct::Type::card)
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
    if (path.right(7) == ".stored") // (type == DfsStruct::Type::stored)
    {
        if (QFile::exists(path))
        {
            if (file.rename(path + ".new"))
                updateFromNewStored(path);
        }
        return;
    }
    file.rename(path);

    QList<QByteArray> pathList = Serialization::deserialize(path.toUtf8() + '/', "/");

    appendToCard(path, pathList.at(PathStruct::aId), type);
    sender->sendFile(path, type, SocketPair());
#ifdef ETALONIUM_CLIENT
    emit usersChanges(path.toUtf8(), type, pathList.at(PathStruct::aId)); // TODO
#endif
}

void Dfs::fileResponse(const QString filePath, const SocketPair &receiver)
{
    DFSMessage::title_message titleMessage(filePath);
    DfsStruct::Type type = getFileType(filePath);
    sender->sendFile(filePath, type, receiver);

    QString storedPath = filePath + DfsStruct::STORED_FILE_NAME;
    if (QFile::exists(storedPath))
        sender->sendFile(storedPath, DfsStruct::Type::stored, receiver);

    /*
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
    */
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
        QDir acDir(DfsStruct::ROOT_FOOLDER_NAME);
        QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
        for (const QString &el : acList)
        {
            QStringList fList = CardManager::getAllFiles(el.toUtf8());
            for (const QString &file : fList)
            {
                DfsStruct::Type ftype = CardManager::getTypeByName(file, el.toUtf8());
                sender->sendFile(file, ftype, receiver);
            }
        }
    }
    QDir dir(DfsStruct::ROOT_FOOLDER_NAME + '/' + actorId);
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
                DfsStruct::Type type = CardManager::getTypeByName(el, actorId);
                if (type != DfsStruct::service)
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

void Dfs::saveStaticFile(QString fileName, DfsStruct::Type type, DfsStruct::SubType subType)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QByteArray sType = DfsStruct::toByteArray(type);
    QString dfsPath = "data/" + userId + "/" + sType + "/" + fileName;

    if (!QFile::exists(dfsPath)) // and no stored
        return;

    bool stored = false;
    if (type == DfsStruct::post || type == DfsStruct::event || type == DfsStruct::service
        || type == DfsStruct::contract || type == DfsStruct::chat)
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

    sender->sendFile(dfsPath + DfsStruct::STORED_FILE_NAME, type, SocketPair());
    sender->sendFile(dfsPath, type, SocketPair());

#ifdef ETALONIUM_CLIENT
    emit usersChanges(dfsPath.toLatin1(), type, userId);
#endif
}

void Dfs::editData(QString userId, QString fileName, DfsStruct::Type type, QByteArray data)
{
    DFSMessage::DfsChanges dfsChanges;
    dfsChanges.userId = userId.toLatin1();
    dfsChanges.changeType = 3;
    dfsChanges.signature = accountControler->getMainActor()->getKey()->encrypt(dfsChanges.userId);
    int pckg = 0;

    QByteArray sType = DfsStruct::toByteArray(type);
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

void Dfs::editSqlDatabase(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                          QByteArrayList sqlChanges)
{
    DFSMessage::DfsChanges dfsChanges;
    dfsChanges.data << sqlChanges;
    dfsChanges.range = "sql";
    dfsChanges.userId = userId.toLatin1();
    QByteArray sType = DfsStruct::toByteArray(type);
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

    if (type == DfsStruct::Bytes)
        apply = applyChangesBytes(dfsChanges);
    else if (type >= DfsStruct::Delete && type <= DfsStruct::Update)
        apply = applyChangesSql(dfsChanges);

    if (apply)
    {
        if (appendToStored(dfsChanges.filePath, Serialization::universalSerialize(dfsChanges.data, 8),
                           dfsChanges.range, dfsChanges.changeType, dfsChanges.userId))
            emit fileChanged(dfsChanges.filePath);
    }

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

    return file3.rename(dfsChanges.filePath);
}

bool Dfs::applyChangesSql(const DFSMessage::DfsChanges &dfsChanges)
{
    // TODO: escape sql & list size checks
    DBConnector db;
    db.open(dfsChanges.filePath.toStdString());
    QByteArrayList data = dfsChanges.data;

    if (dfsChanges.changeType == DfsStruct::Delete)
    {
        QByteArray query = "DELETE FROM " + data[0] + " WHERE " + data[1] + " = '" + data[2] + "'";
        // for (int i = 3; i != data.length(); i += 2)
        //    query += " AND " + data[1] + " = '" + data[2] + "'";
        return db.query(query.toStdString());
    }
    else if (dfsChanges.changeType == DfsStruct::Insert)
    {
        DBRow row;

        for (int i = 1; i < data.length(); i += 2)
        {
            row.insert({ data[i].toStdString(), data[i + 1].toStdString() });
        }
        std::string query = db.prepareInsert(data[0].toStdString(), row);

        if (data.indexOf("message") != -1)
            return db.insertWithData(query, data[data.indexOf("message") + 1]);
        else
            return db.insert(data[0].toStdString(), row);
    }
    else if (dfsChanges.changeType == DfsStruct::Update)
    {
        // QByteArray query = "UPDATE " + data[0] + "SET ... WHERE " + data[1] + " = '" + data[2] + "';";
        return false; // db.update(query.toStdString());
    }

    return false;
}

DfsStruct::Type Dfs::getFileType(const QString &filePath)
{
    QString userId = filePath.mid(5, 20); //
    DBConnector dfsCard(("data/" + userId + "/" + DfsStruct::ACTOR_CARD_FILE).toStdString());
    std::vector<DBRow> res =
        dfsCard.select(("SELECT type FROM " + QByteArray(Config::DataStorage::cardTableName.c_str())
                        + " WHERE path='" + filePath + "';")
                           .toStdString());

    if (!res.empty())
    {
        return DfsStruct::Type(std::stoi(res[0]["type"]));
    }

    return DfsStruct::Type::unknown;
}

void Dfs::process()
{
}

void Dfs::requestFile(const QString &filePath)
{
    if (!QFile::exists(filePath))
    {
        qDebug() << "File is exists";
        return;
    }

    DFSMessage::DfsRequest dfsRequest(filePath); //
    sender->sendDfsMessage(dfsRequest);
}

QByteArray Dfs::buildDfsPath(QByteArray userID, DfsStruct::Type type)
{
    QByteArray sType = DfsStruct::toByteArray(type);
    QByteArray dfsPath = "data/" + userID + "/" + sType + "/";
    BigNumber ss = BigNumber(Config::DataStorage::SECTION_SIZE);
    DBConnector dfsCard(("data/" + userID + "/" + DfsStruct::ACTOR_CARD_FILE).toStdString());
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

bool Dfs::createStored(QString filePath, const QByteArray &userId, const DfsStruct::Type &type)
{
    QString dfsPath = filePath + DfsStruct::STORED_FILE_NAME;
    DBConnector dbc;

    if (!dbc.open((filePath + DfsStruct::STORED_FILE_NAME).toStdString()))
        return false;
    if (!dbc.createTable(Config::DataStorage::storedTableCreation))
        return false;

    return appendToCard(dfsPath, userId, DfsStruct::Type::stored, DfsStruct::SubType::undef);
}

// TODO: update card file
bool Dfs::appendToStored(QString filePath, QByteArray data, QString range, int type, QString userId,
                         bool init)
{
    DBConnector dbc((filePath + DfsStruct::STORED_FILE_NAME).toStdString());
    QByteArray hash = Utils::calcKeccak(
        QByteArray::number(QRandomGenerator::global()->bounded(50000) + QDateTime::currentMSecsSinceEpoch()));
    QByteArray sign = accountControler->getMainActor()->getKey()->sign(userId.toLatin1());

    if (init)
    {
        //        DBRow row = { { "data", data.toStdString() },
        //                      { "range", range.toStdString() },
        //                      { "type", std::to_string(type) },
        //                      { "uid", userId.toStdString() },
        //                      { "sign", sign.toStdString() },
        //                      { "hash", hash.toStdString() },
        //                      { "prevHash", "" } };

        QByteArray q(
            "INSERT OR IGNORE INTO Stored ('hash', 'sign', 'type', 'uid', 'range', 'prevHash', 'data' "
            ") VALUES ('"
            + hash + "', '" + "sign" + "', '" + QByteArray::number(type) + "', '" + userId.toLatin1() + "', '"
            + range.toLatin1() + "', '', ?);");
        return dbc.insertWithData(q.toStdString(), data);
        // return dbc.insert(Config::DataStorage::storedTableName, row);
    }

    QByteArray sep = "', '";
    QByteArray query = "INSERT INTO Stored ('hash', 'sign', 'type', 'uid', 'range', 'prevHash', "
                       "'data') SELECT '"
        + hash + sep + sign + sep + QByteArray::number(type) + sep + userId.toLatin1() + sep
        + range.toLatin1() + "', hash, ? FROM Stored LIMIT 1";

    if (dbc.insertWithData(query.toStdString(), data))
        return updateCard(filePath, userId.toLatin1(),
                          QByteArray::number(QDateTime::currentDateTime().toMSecsSinceEpoch()), hash);
    else
        return false;
}

void Dfs::updateFromNewStored(QString filePath)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QString oldStoredPath = filePath + DfsStruct::STORED_FILE_NAME;
    QString newStoredPath = filePath + DfsStruct::STORED_FILE_NAME + ".new";
    std::string rootPath =
        (DfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/' + DfsStruct::ACTOR_CARD_FILE).toStdString();

    DBConnector dbOld;
    if (dbOld.open(oldStoredPath.toStdString()))
        return;
    auto oldS = dbOld.select("SELECT * FROM Stored");
    dbOld.close();
    DBConnector dbNew;
    if (dbNew.open(newStoredPath.toStdString()))
        return;
    auto newS = dbNew.select("SELECT * FROM Stored");
    dbNew.close();

    QFile::remove(filePath + DfsStruct::STORED_FILE_NAME + ".new");
    if (oldS != newS)
    {
        QString notStored = filePath.left(filePath.length() - 7);
        QFile::remove(notStored);
        QFile::remove(filePath);
        requestFile(notStored);
        requestFile(filePath);
    }
    /*
            return;
            DBConnector dbCardfile;
            if (!dbCardfile.open(rootPath))
                return;

            std::string lastHash = dbCardfile.select("SELECT hash FROM Items")[0]["hash"];

            if (oldS.size() == newS.size())
            {
                if (newS.back().at("hash") == lastHash)
                {
                    return;
                }
                else
                    qDebug() << "WAT";
            }

            if (oldS.size() < newS.size())
            {
                qDebug() << "Houston, something wrong";
                return;
            }

            // diffs

            for (std::size_t i = oldS.size() - 1; i < newS.size(); i++)
            {
                const auto &el = oldS[i];

                DFSMessage::DfsChanges dfsChanges;
                dfsChanges.changeType = std::stoi(el.at("type"));
                dfsChanges.data =
       Serialization::universalDeserialize(QByteArray::fromStdString(el.at("type"))); dfsChanges.range =
       QByteArray::fromStdString(el.at("type"));

                applyChanges(dfsChanges);
            }
    */

    //    QFile::rename(old, new);
}

bool Dfs::updateCard(const QString &path, const QByteArray &userId, QByteArray date, QByteArray newHash)
{
    DBConnector dbc;
    std::string rootPath =
        (DfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/' + DfsStruct::ACTOR_CARD_FILE).toStdString();

    if (!dbc.open(rootPath))
        return false;

    std::string query = QString("UPDATE %1 SET date = '%2', hash = '%3' WHERE path = '%4';")
                            .arg(QString::fromStdString(Config::DataStorage::cardTableName))
                            .arg(QString(date))
                            .arg(QString(newHash))
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
    QDir acDir(DfsStruct::ROOT_FOOLDER_NAME);
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
        DfsStruct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/' + DfsStruct::ACTOR_CARD_FILE;
    if (accountControler->getMainActor() == nullptr)
        return;
    //    DFSMessage::dfs_request rqst(cPath, accountControler->getMainActor()->getId().toActorId());
    //    dfsNetManager->send(rqst.serialize());
}

void Dfs::save(int saveType, QString file, QByteArray data, const DfsStruct::Type type,
               const DfsStruct::SubType subType)
{
    switch (saveType)
    {
    case DfsStruct::DfsSave::File:
        saveToDFS(file, data, type, subType);
        break;
    case DfsStruct::DfsSave::Static:
        saveStaticFile(file, type, subType);
        break;
    case DfsStruct::DfsSave::Network:
        saveFN(file + DfsStruct::FILE_IDENTIFICATOR, file, type);
    }
}
