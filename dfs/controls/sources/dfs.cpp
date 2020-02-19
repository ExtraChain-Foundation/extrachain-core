#include "dfs/controls/headers/dfs.h"

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

void Dfs::responseRequestLast(const DistFileSystem::requestLast &request, SocketPair receiver)
{
    if (request.isEmpty())
        return;

    QByteArrayList res;

    for (QByteArray userId : request.actors)
    {
        QString lastCacheName =
            QString("%1/%2/root.last").arg(QString(DfsStruct::ROOT_FOOLDER_NAME)).arg(QString(userId));
        QByteArray lastHash;

        QFile file(lastCacheName);
        if (file.open(QFile::ReadOnly))
        {
            lastHash = file.readAll();

            if (!lastHash.isEmpty())
                res << userId + " " + lastHash;
        }
    }

    DistFileSystem::responseLast responseLast;
    responseLast.lasts = res;

    sender->sendDfsMessage(responseLast, Messages::DFSMessage::responseLast, receiver);
}

void Dfs::responseResponseLast(const DistFileSystem::responseLast &response, SocketPair receiver)
{
    if (response.isEmpty())
        return;

    QByteArrayList needUpdate;

    for (QByteArray last : response.lasts)
    {
        auto l = last.split(' ');
        QByteArray userId = l[0];
        QByteArray lastHash = l[1];

        QString lastCacheName =
            QString("%1/%2/root.last").arg(QString(DfsStruct::ROOT_FOOLDER_NAME)).arg(QString(userId));
        QFile file(lastCacheName);
        if (!file.open(QFile::ReadOnly))
            continue;
        QByteArray lastHashFile = file.readAll();

        if (lastHash.isEmpty())
            return;

        if (lastHash != lastHashFile)
        {
            needUpdate << userId;

            DistFileSystem::RequestCardPart request;
            request.actorId = userId;
            request.count = 100;
            request.offset = 0;
            sender->sendDfsMessage(request, Messages::DFSMessage::requestCardPath, receiver);
        }
    }

    // qDebug() << "needUpdate" << needUpdate;
}

void Dfs::responseRequestCardPath(const DistFileSystem::RequestCardPart &request, SocketPair receiver)
{
    if (request.isEmpty())
        return;

    qDebug() << "responseRequestCardPath";

    CardFile cardFile(request.actorId);
    if (!cardFile.open())
        return;

    auto data = cardFile.select(request.count, request.offset);
    QByteArrayList res;

    for (auto row : data)
    {
        res << (QByteArrayList() << row["key"].c_str() << row["id"].c_str() << row["type"].c_str()
                                 << row["prevId"].c_str() << row["nextId"].c_str() << row["sign"].c_str())
                   .join(' ');
    }

    if (res.isEmpty())
        return;

    DistFileSystem::ResponseCardPart response;
    response.actorId = request.actorId;
    response.count = request.count;
    response.offset = request.offset;
    response.data = res;
    sender->sendDfsMessage(response, Messages::DFSMessage::responseCardPath, receiver);
}

// TODO: send changes after merge
void Dfs::responseResponseCardPath(const DistFileSystem::ResponseCardPart &response, SocketPair receiver)
{
    if (response.isEmpty())
        return;

    qDebug() << "responseResponseCardPath";

    CardFile cardFile(response.actorId);
    if (!cardFile.open())
        return;

    std::vector<DBRow> local = cardFile.select(response.count, response.offset);
    std::vector<DBRow> network;

    for (auto el : response.data)
    {
        auto list = el.split(' ');
        int networkKey = list[0].toInt();
        QByteArray networkFileId = list[1];
        int networkType = list[2].toInt();
        QByteArray networkPrevId = list[3];
        QByteArray networkNextId = list[4];
        QByteArray networkSign = list[5];

        DBRow row { { "key", std::to_string(networkKey) },     { "id", networkFileId.toStdString() },
                    { "type", std::to_string(networkType) },   { "prevId", networkPrevId.toStdString() },
                    { "nextId", networkNextId.toStdString() }, { "sign", networkSign.toStdString() } };
        network.push_back(row);
    }

    if (local.size() >= network.size())
        return;

    auto last = local.back();
    bool ins = false;
    for (auto el : network)
    {
        if (ins)
            cardFile.append(el.at("id").c_str(), std::stoi(el.at("type").c_str()), el.at("sign").c_str());
        if (last["id"] == el["id"])
        {
            ins = true;
        }
    }

    dfsValidate(response.actorId);

#ifdef ETALONIUM_CONSOLE
    // sender->sendDfsMessage(response, Messages::DFSMessage::responseCardPath, receiver);
#endif
}
// int key = -1;
// QByteArray actorId;
// QByteArray fileId;
// QByteArray prevId;
// QByteArray nextId;
// int type = -1;
// QByteArray sign;
void Dfs::applyCardFileChange(DistFileSystem::CardFileChange cfc, SocketPair receiver)
{ //
#ifdef ETALONIUM_CONSOLE
    sender->sendDfsMessage(cfc, Messages::DFSMessage::cardFileChange);
#endif

    if (QFile::exists(CardManager::buildPathForFile(cfc.actorId.toStdString(), cfc.fileId.toStdString(),
                                                    DfsStruct::Type(cfc.type), false)
                          .c_str()))
        return;

    if (cfc.isEmpty())
    {
        qDebug() << "cardFileChange = empty";
    }
    else
    {
        qDebug() << "cardFileChange =" << cfc.fileId << cfc.type;
    }

    DBConnector dbc(
        (DfsStruct::ROOT_FOOLDER_NAME + "/" + cfc.actorId + "/" + DfsStruct::ACTOR_CARD_FILE + ".future")
            .toStdString());
    dbc.createTable(Config::DataStorage::cardTableCreation);

    DBRow row = { { "key", std::to_string(cfc.key) },     { "id", cfc.fileId.toStdString() },
                  { "prevId", cfc.prevId.toStdString() }, { "nextId", cfc.nextId.toStdString() },
                  { "type", std::to_string(cfc.type) },   { "sign", cfc.sign.toStdString() } };
    bool res = dbc.insert(Config::DataStorage::cardTableName, row);
    qDebug() << "Save to future" << res;

    std::string file =
        CardManager::buildPathForFile(cfc.actorId.toStdString(), row["id"], DfsStruct::Type(cfc.type), false);
    requestFile(QString::fromStdString(file), receiver);

    //    auto type = DfsStruct::Type(cardFileChange.type);

    //    CardFile cardFile(cardFileChange.actorId);
    //    if (!cardFile.open())
    //        return;

    //    bool result = cardFile.append(cardFileChange.fileId, type, cardFileChange.sign, false);

    //    if (result)
    //    {
    //        dfsValidate(cardFileChange.actorId); // TODO: delay timer
    //    }
}

void Dfs::initDFS(const QByteArray &userId)
{
    QDir().mkdir(DfsStruct::ROOT_FOOLDER_NAME);
    QDir().mkdir(DfsStruct::ROOT_FOOLDER_NAME + '/' + userId);
    QList<QByteArray> subPathList = { "/images/", "/video/",    "/events/",  "/chats/",
                                      "/posts/",  "/services/", "/private/", "/files/" };

    DBConnector dbc(
        (DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/" + DfsStruct::ACTOR_CARD_FILE + ".future")
            .toStdString());
    dbc.createTable(Config::DataStorage::cardTableCreation);

    for (const QByteArray &currentPath : subPathList)
        QDir().mkpath(DfsStruct::ROOT_FOOLDER_NAME + '/' + userId + currentPath);

    qDebug() << "[init dfs for user]" << userId;
    // signalConnections();
    qDebug() << "[init finished]";
    requestCardById(userId);
}

void Dfs::saveToDFS(const QString &path, const QByteArray &data, const DfsStruct::Type &type)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QByteArray dfsPath;
    bool stored = false;
    bool exists = false;

    if (path.isEmpty()) // if !path AND data
    {
        dfsPath = buildDfsPath("", Utils::calcKeccak(data), userId, type);
        exists = QFile::exists(dfsPath);

        if (!exists)
        {
            QFile file(dfsPath);
            file.open(QFile::WriteOnly);
            file.write(data);
            file.close();
        }
    }
    else // if path
    {
        dfsPath = buildDfsPath(path, "", userId, type);
        exists = QFile::exists(dfsPath);

        if (!exists)
        {
            bool needCopy = false;

#ifdef ETALONIUM_CLIENT
            if (type == DfsStruct::Type::Image)
            {
                QImageReader imageReader(path);
                QByteArray format = imageReader.format();
                bool isIosImage = (format == "heic" || format == "heif");

                if (isIosImage)
                {
                    imageReader.setAutoTransform(true);
                    QImage image = imageReader.read();
                    needCopy = image.save(dfsPath, "JPG");
                }
            }
#endif

            if (!needCopy)
            {
                QFile file(path);
                if (!file.copy(dfsPath))
                {
                    QFile::remove(dfsPath);
                    file.copy(dfsPath);
                }
            }
        }
    }

    if (exists)
        return;

    if (!appendToCard(dfsPath, userId, type, true))
        return;

    if (type == DfsStruct::Post || type == DfsStruct::Event || type == DfsStruct::Service
        || type == DfsStruct::Contract || type == DfsStruct::Chat)
    {
        if (!createStored(dfsPath, userId, type))
        {
            return;
        }
        else
        {
            stored = true;
            QString range = QString("0:%1").arg(data.size());
            QByteArray hash = Utils::calcKeccak(QByteArray::number(QRandomGenerator::global()->bounded(50000)
                                                                   + QDateTime::currentMSecsSinceEpoch()));
            appendToStored(dfsPath, data, range, 3, userId, true, hash);
        }
    }

    // if (stored)
    //     sender->sendFile(dfsPath + DfsStruct::STORED_EXT, type, SocketPair());
    // sender->sendFile(dfsPath, type, SocketPair());

    CardFile cardFile(userId);
    if (!cardFile.open())
    {
        qDebug() << "VAH";
        std::exit(-1);
        return;
    }

    auto lastRes = cardFile.last();
    if (!lastRes)
    {
        qDebug() << "!lastRes";
        std::exit(1);
    }
    DBRow last = *lastRes;

    if (last.empty())
    {
        qDebug() << "last.empty";
        std::exit(1);
    }

    DistFileSystem::CardFileChange cardFileChange;
    cardFileChange.key = std::stoi(last["key"]);
    cardFileChange.actorId = userId;
    cardFileChange.fileId = last["id"].c_str();
    cardFileChange.prevId = last["prevId"].c_str();
    cardFileChange.nextId = last["nextId"].c_str();
    cardFileChange.type = std::stoi(last["type"]);
    cardFileChange.sign = last["sign"].c_str();
    if (cardFileChange.fileId.isEmpty())
    {
        qDebug() << "empty file id";
        std::exit(1);
    }
    sender->sendDfsMessage(cardFileChange, Messages::DFSMessage::cardFileChange);
    qDebug() << "Send root change" << cardFileChange.fileId << cardFileChange.type;

#ifdef ETALONIUM_CLIENT
    emit usersChanges(dfsPath, type, userId); // TODO
#endif
}

bool Dfs::appendToCard(const QString &path, const QByteArray &userId, const DfsStruct::Type &type,
                       bool isFilePath)
{
    QByteArray sign = accountControler->getMainActor()->getKey()->sign(
        (isFilePath ? CardManager::cutPath(path) : path).toUtf8()); //

    CardFile cardFile(userId);
    if (!cardFile.open())
        return false;

    bool result = cardFile.append(path.toUtf8(), type, sign, isFilePath);
    return result;
}

void Dfs::getDFSStatus()
{
}

// не создавать рут
void Dfs::saveFN(const QString tmpPath, const QString &path, const DfsStruct::Type &type)
{
    QFile file(tmpPath);

    if (!QFile::exists(tmpPath))
    {
        qDebug() << "Thes es ochen ploho" << tmpPath;
        return;
    }

    // TODO: no insert to dfs file with size = 0
    //    if (QFile(tmpPath).size() == 0)
    //    {
    //        if (path.right(5) == "/root")
    //        {
    //            QFile::remove(tmpPath);
    //            requestFile(path);
    //        }
    //        return;
    //    }

    if (path.right(5) == "/root" && path.length() == 30) // (type == DfsStruct::Type::root)
    {
        if (!QFile::exists(path))
        {
            if (QFile::rename(tmpPath, path))
            {
                {
                    CardFile cardFile(path.mid(5, 20));
                    if (cardFile.open())
                        cardFile.updateLastCache();
                }
                dfsValidate(path.split("/")[1].toUtf8());
            }
        }
        return;
    }

    if (path.right(7) == ".stored") // (type == DfsStruct::Type::stored)
    {
        if (QFile::exists(path))
        {
            if (file.rename(path + ".new"))
                updateFromNewStored(path);
            return;
        }
    }
    file.rename(path);
    if (file.isOpen())
        file.close();

    int indx = m_tmpFiles.indexOf(path);
    if (indx != -1)
        m_tmpFiles.removeAt(indx);

    QList<QByteArray> pathList = Serialization::deserialize(path.toUtf8() + '/', "/");

    if (path.right(7) != ".stored")
    {
        // TODO: check types
        requestFile(path + ".stored");
    }

    qDebug() << "File received:" << path;

    QByteArray userId = path.split("/")[1].toUtf8();
    QString fileId = CardManager::cutPath(path);
    QString cardFile = "data/" + userId + "/root";

    if (QFile::exists(cardFile))
    {
        DBConnector rootFuture;
        if (!rootFuture.open(cardFile.toStdString() + ".future"))
        {
            return;
        }

        auto itemFuture = rootFuture.select("SELECT * FROM Items WHERE id = '" + fileId.toStdString() + "'");

        if (!itemFuture.empty())
        {
            rootFuture.deleteRow("Items", "id", fileId.toStdString());
            rootFuture.close();
            DBConnector rootDb(cardFile.toStdString());
            rootDb.insert("Items", itemFuture[0]);
            rootDb.close();

            DistFileSystem::CardFileChange cardFileChange;
            cardFileChange.key = std::stoi(itemFuture[0]["key"]);
            cardFileChange.actorId = userId;
            cardFileChange.fileId = itemFuture[0]["id"].c_str();
            cardFileChange.prevId = itemFuture[0]["prevId"].c_str();
            cardFileChange.nextId = itemFuture[0]["nextId"].c_str();
            cardFileChange.type = std::stoi(itemFuture[0]["type"]);
            cardFileChange.sign = itemFuture[0]["sign"].c_str();
            sender->sendDfsMessage(cardFileChange, Messages::DFSMessage::cardFileChange);
        }
        else
        {
            rootFuture.close();
        }
    }

    this->dfsValidate(userId);

#ifdef ETALONIUM_CLIENT
    emit usersChanges(path.toUtf8(), type, pathList.at(PathStruct::aId)); // TODO
    if (type == DfsStruct::Type::Post && !path.contains(".stored"))
        emit newNotify({ QDateTime::currentMSecsSinceEpoch(), notification::NotifyType::NewPost,
                         pathList.at(PathStruct::aId) + " " + pathList.at(PathStruct::name) });
#endif
}

void Dfs::fileResponse(const QString filePath, const SocketPair &receiver)
{
    qDebug() << "File request response:" << filePath;
    DfsStruct::Type type = getFileType(filePath);
    if (type == DfsStruct::Type::Error)
    {
        qDebug() << "Card file for response not exits";
        return;
    }
    // qDebug() << "fileResponse";
    sender->sendFile(filePath, type, receiver);

    QString storedPath = filePath + DfsStruct::STORED_EXT;
    if (QFile::exists(storedPath))
        sender->sendFile(storedPath, DfsStruct::Type::Stored, receiver);

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
    if (sender == nullptr)
        return;
    sender->sendFragments(path, CardManager::getTypeByName(path), frags, receiver);
}

Dfs::Dfs(ActorIndex *actorIndex, AccountController *accControler, QObject *parent)
    : QObject(parent)
    , accountControler(accControler)
    , actorIndex(actorIndex)
{
    connect(this, &Dfs::sendFromNetwork, this, &Dfs::save, Qt::QueuedConnection);
    connect(this, &Dfs::requestFile, this, &Dfs::requestFileHandle);
    connect(this, &Dfs::titleReceived, this, &Dfs::titleReceivedHandle);
}

Dfs::~Dfs()
{
    sender->deleteLater();
}

void Dfs::initDFSNetManager()
{
    dfsNetManager = new DFSNetManager(accountControler, actorIndex);
    dfsNetManager->setDfs(this);
    connect(this, &Dfs::connectToServer, dfsNetManager, &DFSNetManager::uiReconnect);
    ThreadPool::addThread(dfsNetManager);
}

void Dfs::saveStaticFile(QString fileName, DfsStruct::Type type, bool needStored)
{
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    QByteArray sType = DfsStruct::toByteArray(type);
    QString dfsPath = "data/" + userId + "/" + sType + "/" + fileName;

    if (!QFile::exists(dfsPath)) // and no stored
        return;

    bool stored = false;
    if (needStored
        && (type == DfsStruct::Post || type == DfsStruct::Event || type == DfsStruct::Service
            || type == DfsStruct::Contract || type == DfsStruct::Chat || type == DfsStruct::Private))
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
            // DFSMessage::DfsChanges dfsChanges(dfsPath, { data }, range, 3, userId, userId);
            bool card = appendToCard(dfsPath, userId, DfsStruct::Type(static_cast<int>(type) + 100), true);
            QByteArray hash = Utils::calcKeccak(QByteArray::number(QRandomGenerator::global()->bounded(50000)
                                                                   + QDateTime::currentMSecsSinceEpoch()));
            bool stored = appendToStored(dfsPath, data, range, 3, userId, true, hash);

            if (!card)
                return;
            if (!stored)
                return;
        }
    }

    CardFile cardFile(userId);
    if (!cardFile.open())
    {
        qDebug() << "VAH";
        std::exit(-1);
        return;
    }

    auto lastRes = cardFile.last();
    if (!lastRes)
        return;
    DBRow last = *lastRes;

    DistFileSystem::CardFileChange cardFileChange;
    cardFileChange.key = std::stoi(last["key"]);
    cardFileChange.actorId = userId;
    cardFileChange.fileId = last["id"].c_str();
    cardFileChange.prevId = last["prevId"].c_str();
    cardFileChange.nextId = last["nextId"].c_str();
    cardFileChange.type = std::stoi(last["type"]);
    cardFileChange.sign = last["sign"].c_str();
    sender->sendDfsMessage(cardFileChange, Messages::DFSMessage::cardFileChange);

#ifdef ETALONIUM_CLIENT
    emit usersChanges(dfsPath.toLatin1(), type, userId);
#endif
}

void Dfs::editData(QString userId, QString fileName, DfsStruct::Type type, QByteArray data)
{
    DistFileSystem::DfsChanges dfsChanges;
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
        auto readed = file.read(DistFileSystem::dataSize);

        QByteArray newDataPart = data.mid(DistFileSystem::dataSize * pckg, DistFileSystem::dataSize);
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

    if (data.size() > DistFileSystem::dataSize * pckg)
    {
        int totalPckg = (data.size() - DistFileSystem::dataSize * pckg) / DistFileSystem::dataSize + pckg;

        for (int i = pckg; i <= totalPckg; ++i)
        {
            pckgNums << QByteArray::number(i);
            dfsChanges.data << data.mid(DistFileSystem::dataSize * i, DistFileSystem::dataSize);
        }
    }

    dfsChanges.range = pckgNums.join(" ");
    dfsChanges.messHash = Utils::calcKeccak(
        QByteArray::number(QRandomGenerator::global()->bounded(50000) + QDateTime::currentMSecsSinceEpoch()));
    pckgNums.clear();

    qDebug() << dfsChanges.range;
    qDebug() << dfsChanges.data;

    if (applyChanges(dfsChanges))
        sender->sendDfsMessage(dfsChanges, Messages::DFSMessage::changesMessage);
}

void Dfs::editSqlDatabase(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                          QByteArrayList sqlChanges)
{
    DistFileSystem::DfsChanges dfsChanges;
    dfsChanges.data << sqlChanges;
    dfsChanges.range = "sql";
    dfsChanges.userId = userId.toLatin1();
    QByteArray sType = DfsStruct::toByteArray(type);
    dfsChanges.filePath = "data/" + dfsChanges.userId + "/" + sType + "/" + fileName;
    dfsChanges.signature = accountControler->getMainActor()->getKey()->encrypt(dfsChanges.userId);
    dfsChanges.changeType = sqlType;
    dfsChanges.messHash = Utils::calcKeccak(
        QByteArray::number(QRandomGenerator::global()->bounded(50000) + QDateTime::currentMSecsSinceEpoch()));

    if (applyChanges(dfsChanges))
    {
    }
    sender->sendDfsMessage(dfsChanges, Messages::DFSMessage::changesMessage);
}

bool Dfs::applyChanges(const DistFileSystem::DfsChanges &dfsChanges)
{
    int type = dfsChanges.changeType;
    bool apply = false;

    if (!QFile::exists(dfsChanges.filePath))
    {
        // sender->sendDfsMessage(dfsChanges);
        return false;
    }
    if (!QFile::exists(dfsChanges.filePath + ".stored"))
    {
        // sender->sendDfsMessage(dfsChanges);
        return false;
    }

    DBConnector db;
    if (!db.open(dfsChanges.filePath.toStdString() + ".stored"))
        return false;
    QByteArray check = "SELECT * FROM Stored WHERE hash = '" + dfsChanges.messHash + "'";
    std::vector<DBRow> checkHash = db.select(check.toStdString());
    if (checkHash.size() != 0)
    {
        qDebug() << "Already have hash" << dfsChanges.messHash;
        return false;
    }

    if (type == DfsStruct::Bytes)
        apply = applyChangesBytes(dfsChanges);
    else if (type >= DfsStruct::Delete && type <= DfsStruct::Update)
        apply = applyChangesSql(dfsChanges);

    if (apply)
    {
        if (appendToStored(dfsChanges.filePath, Serialization::universalSerialize(dfsChanges.data, 8),
                           dfsChanges.range, dfsChanges.changeType, dfsChanges.userId, false,
                           dfsChanges.messHash))
        {
            emit fileChanged(dfsChanges.filePath);
            return true;
        }
    }

    return false;
}

bool Dfs::applyChangesBytes(const DistFileSystem::DfsChanges &dfsChanges)
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
        int pos = DistFileSystem::dataSize * i;

        int indexOf = pckgNums.indexOf(QByteArray::number(i));
        if (indexOf != -1)
        {
            file3.write(dfsChanges.data[indexOf]);
        }
        else
        {
            file.seek(pos);
            file3.write(file.read(DistFileSystem::dataSize));
        }
    }

    file.close();
    file3.close();
    file.remove();

    return file3.rename(dfsChanges.filePath);
}

bool Dfs::applyChangesSql(const DistFileSystem::DfsChanges &dfsChanges)
{
    // TODO: escape sql & list size checks
    DBConnector db;
    db.open(dfsChanges.filePath.toStdString());
    QByteArrayList data = dfsChanges.data;

    if (dfsChanges.changeType == DfsStruct::Delete)
    {
        QByteArray query = "DELETE FROM " + data[0] + " WHERE " + data[1] + " = '" + data[2] + "'";

        if (data.length() > 3)
            query += " AND " + data[3] + " = '" + data[4] + "'";
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
    return CardManager::getTypeByName(filePath);
}

QStringList Dfs::tmpFiles() const
{
    return m_tmpFiles;
}

void Dfs::dfsSyncUsers(QList<QString> userID, const SocketPair &receiver)
{
    for (QString s : userID)
    {
        //        if (dfsValidate(s.toUtf8()))
        //        {
        requestCardById(s.toLatin1(), receiver);
        //        }
    }
}

void Dfs::dfsSyncT()
{
    if (accountControler->getMainActor() == nullptr)
        return;
    dfsValidateAll();
    QByteArray mainActor = accountControler->getMainActor()->getId().toActorId();
    QString myCardFile = "data/" + mainActor + "/" + DfsStruct::ACTOR_CARD_FILE;
    QStringList reqCards;
    QDir acDir(DfsStruct::ROOT_FOOLDER_NAME);
    QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
    dfsSyncUsers(acList);
}

void Dfs::dfsSync(const SocketPair &receiver)
{
    //    // request other roots

    //    //    QByteArrayList aList = actorIndex->allActors();
    //    //    QList<QByteArray>::iterator it;
    //    //    it = aList.begin();
    //    //    while (it != aList.end())
    //    //    {
    //    //        Actor<KeyPublic> actor = actorIndex->getActor(BigNumber(*it));
    //    //        if (actor.isEmpty())
    //    //        {
    //    //            aList.removeOne(*it);
    //    //        }
    //    //        else if (actor.getAccount() != actorType::ACCOUNT)
    //    //        {
    //    //            aList.removeOne(*it);
    //    //        }
    //    //        else
    //    //        {
    //    //            ++it;
    //    //        }
    //    //    }
    //    if (accountControler->getMainActor() == nullptr)
    //        return;

    //    QByteArray mainActor = accountControler->getMainActor()->getId().toActorId();
    //    QString myCardFile = "data/" + mainActor + "/" + DfsStruct::ACTOR_CARD_FILE;
    //    QStringList reqCards;
    //    QDir acDir(DfsStruct::ROOT_FOOLDER_NAME);
    //    QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
    //    //    int pos = acList.indexOf(mainActor);
    //    //    if (pos != -1)
    //    //        acList.removeAt(pos);
    //    dfsSyncUsers(acList, receiver);
}

bool Dfs::dfsValidate(QByteArray userID)
{
    QString cardFile = DfsStruct::ROOT_FOOLDER_NAME + "/" + userID + "/" + DfsStruct::ACTOR_CARD_FILE;
    QString profile =
        DfsStruct::ROOT_FOOLDER_NAME + "/" + userID + "/profile/" + userID + DfsStruct::PROFILE_EXT;
    QString chatinvite = DfsStruct::ROOT_FOOLDER_NAME + "/" + userID + "/services/" + DfsStruct::CHATINVITE;
    QString chatinvite_s = DfsStruct::ROOT_FOOLDER_NAME + "/" + userID + "/services/" + DfsStruct::CHATINVITE
        + DfsStruct::STORED_EXT;
    QString follower = DfsStruct::ROOT_FOOLDER_NAME + "/" + userID + "/services/" + DfsStruct::FOLLOWER;
    QString follower_s = DfsStruct::ROOT_FOOLDER_NAME + "/" + userID + "/services/" + DfsStruct::FOLLOWER
        + DfsStruct::STORED_EXT;
    QString subscribe = DfsStruct::ROOT_FOOLDER_NAME + "/" + userID + "/services/" + DfsStruct::SUBSCRIBE;
    QString subscribe_s = DfsStruct::ROOT_FOOLDER_NAME + "/" + userID + "/services/"
        + DfsStruct::ACTOR_CARD_FILE + DfsStruct::STORED_EXT;

    if (!(actorIndex->hasActor(BigNumber(userID))))
    {
        return false;
    }

    //    if (!QFile::exists(cardFile))
    //    {
    //        return false;
    //    }

    //    if (!QFile::exists(chatinvite) && !QFile::exists(chatinvite_s) && !QFile::exists(follower)
    //        && !QFile::exists(follower_s) && !QFile::exists(subscribe) && !QFile::exists(subscribe_s)
    //        /* && !QFile::exists(profile)*/)
    //    {
    //        return false;
    //    }
    if (!QFile::exists(cardFile))
        return false;
    DBConnector root;
    if (!root.open(cardFile.toStdString()))
    {
        return false;
    }
    DBConnector rootFuture;
    if (!rootFuture.open(cardFile.toStdString() + ".future"))
    {
        return false;
    }

    auto itemsFuture = QVector<DBRow>::fromStdVector(rootFuture.select("SELECT * FROM Items"));
    auto items = QVector<DBRow>::fromStdVector(root.select("SELECT * FROM Items"));
    root.close();
    rootFuture.close();
    items << itemsFuture;

    if (!items.empty())
    {
        std::string fPath;
        bool flag = true;
        for (DBRow &item : items)
        {
            if (item["id"].empty())
                continue;
            fPath = CardManager::buildPathForFile(userID.toStdString(), item["id"],
                                                  DfsStruct::Type(std::stoi(item["type"])), false);
            QFileInfo file(QString::fromStdString(fPath));
            if (!file.exists() || file.size() == 0)
            {
                requestFile(QString::fromStdString(fPath));
                flag = false;
            }
        }
        return flag;
    }
    else
    {
        return true;
    }
}

QList<QByteArray> Dfs::dfsValidateAll()
{
    QByteArray mainActor = accountControler->getMainActor()->getId().toActorId();
    //    QString myCardFile = "data/" + mainActor + "/" + DfsStruct::ACTOR_CARD_FILE;
    QStringList reqCards;
    QDir acDir(DfsStruct::ROOT_FOOLDER_NAME);
    QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
    //    int pos = acList.indexOf(mainActor);
    //    if (pos != -1)
    //        acList.removeAt(pos);
    QList<QByteArray> res;
    for (QString user : acList)
    {
        if (!dfsValidate(user.toUtf8()))
        {
            res.append(user.toUtf8());
        }
    }
    return res;
}

void Dfs::process()
{
}

void Dfs::startDFS()
{
    QByteArrayList actors = actorIndex->allActors();
    for (const QByteArray &actor : actors)
        initDFS(actor);

    initDFSNetManager();
    if (sender == nullptr)
    {
        sender = new Sender();
        sender->setNetManager(dfsNetManager);
        ThreadPool::addThread(sender);
    }

    timerTmpFiles = new QTimer(this);
    //    static QTimer TmpTimer;
    connect(timerTmpFiles, &QTimer::timeout, this, &Dfs::searchTmp);
    searchTmp();
    timerTmpFiles->start(5000);

    emit networkCreated();
}

void Dfs::requestFileHandle(const QString &filePath, const SocketPair &receiver)
{
    // if (QFile::exists(filePath))
    // {
    //    qDebug() << "File is exists";
    //     return;
    // }
    if (dfsNetManager == nullptr || sender == nullptr)
    {
        qDebug().nospace() << "What's up, Doc? " << (dfsNetManager == nullptr ? "dfsNetManager" : "sender")
                           << " == nullptr";
        return;
    }

    if (filePath.indexOf("/root") != -1)
    {
        qDebug() << "root requested";
    }

    if (dfsNetManager->isLoading(filePath))
        return;

    //    if (m_reqFiles.indexOf(filePath) != -1)
    //        return;
    auto it = std::find_if(m_reqFiles.begin(), m_reqFiles.end(),
                           [filePath](std::pair<qint64, QString> pair) { return pair.second == filePath; });
    if (it != m_reqFiles.end())
    {
        qint64 current = QDateTime::currentDateTime().toMSecsSinceEpoch();
        qint64 reqDate = it->first;
        if (reqDate + 5000 <= current)
        {
            // m_reqFiles.erase(it);
        }
        else
            return;
    }

    m_reqFiles.append({ QDateTime::currentDateTime().toMSecsSinceEpoch(), filePath });
    qDebug() << "Request file:" << filePath; // TODO: fix for company actor
    DistFileSystem::DfsRequest dfsRequest;
    dfsRequest.filePath = filePath;
    sender->sendDfsMessage(dfsRequest, Messages::DFSMessage::requestMessage, receiver);
}

void Dfs::titleReceivedHandle(QString filePath)
{
    //    auto it = std::find_if(m_reqFiles.begin(), m_reqFiles.end(),
    //                           [filePath](std::pair<qint64, QString> pair) { return pair.second == filePath;
    //                           });
    //    if (it != m_reqFiles.end())
    //    {
    //        m_reqFiles.erase(it);
    //    }
    //    qDebug() << "m_reqFiles" << m_reqFiles.length();
}

QByteArray Dfs::buildDfsPath(QString originalFile, QByteArray hash, QByteArray userID, DfsStruct::Type type)
{
    QByteArray sType = DfsStruct::toByteArray(type);
    QByteArray dfsPath = "data/" + userID + "/" + sType + "/";

    QByteArray fileHash = hash.isEmpty() ? Utils::calcKeccakForFile(originalFile) : hash;
    dfsPath += fileHash.right(2);
    QDir().mkpath(dfsPath);
    dfsPath += "/" + fileHash;
    return dfsPath;
}

bool Dfs::createStored(QString filePath, const QByteArray &userId, const DfsStruct::Type &type)
{
    QString dfsPath = filePath + DfsStruct::STORED_EXT;
    DBConnector dbc;

    if (!dbc.open((filePath + DfsStruct::STORED_EXT).toStdString()))
        return false;
    if (!dbc.createTable(Config::DataStorage::storedTableCreation))
        return false;

    return true;
}

// TODO: update card file
bool Dfs::appendToStored(QString filePath, QByteArray data, QString range, int type, QString userId,
                         bool init, QByteArray hash)
{
    DBConnector dbc((filePath + DfsStruct::STORED_EXT).toStdString());
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
            + hash + "', '" + sign + "', '" + QByteArray::number(type) + "', '" + userId.toLatin1() + "', '"
            + range.toLatin1() + "', '', ?);");
        return dbc.insertWithData(q.toStdString(), data);
        // return dbc.insert(Config::DataStorage::storedTableName, row);
    }

    QByteArray sep = "', '";
    QByteArray query = "INSERT INTO Stored ('hash', 'sign', 'type', 'uid', 'range', 'prevHash', "
                       "'data') SELECT '"
        + hash + sep + sign + sep + QByteArray::number(type) + sep + userId.toLatin1() + sep
        + range.toLatin1() + "', hash, ? FROM Stored LIMIT 1";

    return dbc.insertWithData(query.toStdString(), data);
}

void Dfs::updateFromNewStored(QString filePath)
{ // TODO: attach
    qDebug() << "Looking for difference in Stored:" << filePath;
    QString oldStoredPath = filePath;
    QString newStoredPath = filePath + ".new";
    QString userId = filePath.mid(5, 20);
    std::string rootPath =
        (DfsStruct::ROOT_FOOLDER_NAME + '/' + userId + '/' + DfsStruct::ACTOR_CARD_FILE).toStdString();

    DBConnector dbOld;
    if (!dbOld.open(oldStoredPath.toStdString()))
    {
        QFile::remove(newStoredPath);
        return;
    }
    auto oldS = dbOld.select("SELECT * FROM Stored");
    dbOld.close();
    DBConnector dbNew;
    if (!dbNew.open(newStoredPath.toStdString()))
    {
        QFile::remove(newStoredPath);
        return;
    }
    auto newS = dbNew.select("SELECT * FROM Stored");
    dbNew.close();

    if (oldS.size() > newS.size())
    {
        QFile::remove(newStoredPath);
        return;
    }

    if (oldS == newS)
    {
        QFile::remove(newStoredPath);
    }
    else
    {
        QString notStored = filePath.left(filePath.length() - 7);
        QString notStoredNew = notStored + ".new";

        //
        std::vector<DBRow> rows;
        QByteArray table;

        for (const DBRow &stored : newS) // filePath // changeType // data
        {
            int type = std::stoi(stored.at("type"));

            if (type == 3)
            {
                QFile file(notStoredNew);

                if (!file.open(QFile::WriteOnly))
                {
                    qDebug() << "-------> VSE PROPALO";
                    return;
                }

                // qDebug() << QByteArray::fromStdString(stored.at("data")) << stored.size();
                file.write(QByteArray::fromStdString(stored.at("data")));
                file.close();
                continue;
            }

            QByteArray data = QByteArray::fromStdString(stored.at("data"));
            QByteArray range = QByteArray::fromStdString(stored.at("range"));
            QByteArray userId = stored.at("uid").c_str();

            switch (type)
            {
            case DfsStruct::ChangeType::Insert: {
                QByteArrayList list = Serialization::universalDeserialize(data, 8);
                table = list[0];
                DBRow row;
                for (int i = 1; i < list.length(); i += 2)
                    row.insert({ list[i].toStdString(), list[i + 1].toStdString() });
                rows.push_back(row);
                break;
            }
            case DfsStruct::ChangeType::Delete: {
                QByteArrayList list = Serialization::universalDeserialize(data, 8);
                table = list[0];

                for (std::size_t i = 0; i != rows.size(); i++)
                {
                    const auto &r = rows[i];
                    if (r.at(list[1].toStdString()) == list[2].toStdString())
                    {
                        rows.erase(rows.begin() + i);
                        break;
                    }
                }
                break;
            }
            case DfsStruct::ChangeType::Update:
                break;
            }

            // DFSMessage::DfsChanges dfsChanges(notStored, data, range, type, userId, "", "");
        }

        DBConnector db;
        if (!db.open(notStoredNew.toStdString()))
        {
            return;
        }
        for (const auto &row : rows)
        {
            if (filePath.indexOf("/msg.stored") != -1 || filePath.indexOf("/chatinvite.stored") != -1)
            {
                std::string d = row.at("message");
                db.insertWithData(db.prepareInsert(table.toStdString(), row), QByteArray::fromStdString(d));
            }
            else
            {
                db.insert(table.toStdString(), row);
            }
        }
        //

        if (QFile(newStoredPath).size() == 0 || QFile(notStoredNew).size() == 0)
        {
            QFile::remove(newStoredPath);
            QFile::remove(notStoredNew);
            return;
        }

        QFile::remove(filePath);
        QFile::remove(notStored);
        QFile::rename(newStoredPath, filePath);
        QFile::rename(notStoredNew, notStored);
        fileChanged(notStored);
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

void Dfs::initMyLocalStorage()
{
    //    resolver = new DFSResolver(actorIndex);
    //

    //    ThreadPool::addThread(resolver);

    //    getDFSStatus();
    QByteArray userId = accountControler->getMainActor()->getId().toActorId();
    initDFS(userId);
    //    QDir acDir(DfsStruct::ROOT_FOOLDER_NAME);
    //    if (acDir.exists())
    //    {
    //        QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
    //        for (const QString &el : acList)
    //        {
    //            //            if (el.toUtf8() != (*accountControler->getMainActor()).getId().toActorId())
    //            //            {
    //            //                QString cPath = dfsStruct::ROOT_FOOLDER_NAME + '/' + el + '/' +
    //            //                dfsStruct::ACTOR_CARD_FILE; DFSMessage::dfs_request rqst(cPath,
    //            //                accountControler->getMainActor()->getId().toActorId());
    //            //                dfsNetManager->send(rqst.serialize());
    //            //            }
    //        }
    //    }
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

void Dfs::save(int saveType, QString file, QByteArray data, const DfsStruct::Type type)
{
    switch (saveType)
    {
    case DfsStruct::DfsSave::File:
        saveToDFS(file, data, type);
        break;
    case DfsStruct::DfsSave::Static:
        saveStaticFile(file, type, true);
        break;
    case DfsStruct::DfsSave::StaticNonStored:
        saveStaticFile(file, type, false);
        break;
    case DfsStruct::DfsSave::Network:
        saveFN(file + DfsStruct::FILE_IDENTIFICATOR, file, type);
        break;
    }
}

void Dfs::searchTmp()
{
    qint64 current = QDateTime::currentDateTime().toMSecsSinceEpoch();

    for (auto it = m_reqFiles.begin(); it != m_reqFiles.end(); it++)
    {
        qint64 reqDate = it->first;
        if (reqDate + 5000 <= current)
            m_reqFiles.erase(it--);
    }

    qDebug() << "search tmp" << m_reqFiles.length();

    QStringList dataIds = QDir("data").entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &id : dataIds)
    {
        QString rootPath = "data/" + id + "/root";
        QFileInfo root(rootPath);
        // QFileInfo rootTmp(rootPath + ".tmp");

        if (actorIndex->companyId)
        {
            QByteArray companyId = *actorIndex->companyId;

            if (id == companyId)
                continue;
        }

        if (dfsNetManager->isLoading(rootPath))
            return;

        if (!root.exists() || root.size() == 0)
        {
            requestFile(rootPath);
        }
    }

    QDirIterator dirIt("data", QDirIterator::Subdirectories);
    QSet<QString> tmpFiles;

    while (dirIt.hasNext())
    {
        dirIt.next();
        // qDebug() << dirIt.filePath();
        QFileInfo file = QFileInfo(dirIt.filePath());
        if (file.isFile() && QFileInfo(dirIt.filePath()).suffix() == "tmp")
        {
            QString fileName = dirIt.filePath().chopped(4);
            if (fileName.right(5) != "/root")
            {
                // QFile::remove(dirIt.filePath());
                requestFile(fileName);
                // requestFile(fileName + ".last");
            }
            //            else
            //                tmpFiles << fileName;
            //            if (reqFile)
            //            {

            //            }
        }
    }

    //    if (tmpFiles.size() > 0)
    //    {
    //        qDebug() << tmpFiles;
    //        this->m_tmpFiles = tmpFiles.toList();
    //    }
}

void Dfs::requestCardById(QByteArray userId, const SocketPair &receiver)
{
    if (dfsNetManager == nullptr || sender == nullptr)
    {
        qDebug().nospace() << "What's up, Doc? " << (dfsNetManager == nullptr ? "dfsNetManager" : "sender")
                           << " == nullptr";
        return;
    }

    QString fileName = "data/" + userId + "/root";

    qDebug() << ">>>>" << fileName << QFile::exists(fileName) << QFile(fileName).size();
    if (!QFile::exists(fileName) || QFile(fileName).size() == 0)
    {
        requestFile(fileName);
        return;
    }

    DistFileSystem::requestLast requestLast;
    requestLast.actors = actorIndex->allActors();
    sender->sendDfsMessage(requestLast, Messages::DFSMessage::requestLast, receiver);
}

void Dfs::requestAllCards()
{
    if (actorIndex == nullptr)
        return;

    const QByteArrayList allUserIds = actorIndex->allActors();

    for (const QString &id : allUserIds)
        requestCardById(id.toLatin1());
}
