/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

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
        QString lastCacheName = QString("%1/%2/%3")
                                    .arg(DfsStruct::ROOT_FOOLDER_NAME)
                                    .arg(QString(userId))
                                    .arg(DfsStruct::ACTOR_CARD_LAST);
        QByteArray lastHash;

        QFile file(lastCacheName);
        if (file.open(QFile::ReadOnly))
        {
            lastHash = file.readAll();

            if (!lastHash.isEmpty())
                res << userId + " " + lastHash;
        }
    }

    if (res.isEmpty())
        return;
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

        QString lastCacheName = QString("%1/%2/%3")
                                    .arg(DfsStruct::ROOT_FOOLDER_NAME)
                                    .arg(QString(userId))
                                    .arg(DfsStruct::ACTOR_CARD_LAST);
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
                                 << row["prevId"].c_str() << row["nextId"].c_str() << row["version"].c_str()
                                 << row["sign"].c_str())
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
        QByteArray networkVersion = list[5];
        QByteArray networkSign = list[6];

        DBRow row { { "key", std::to_string(networkKey) },     { "id", networkFileId.toStdString() },
                    { "type", std::to_string(networkType) },   { "prevId", networkPrevId.toStdString() },
                    { "nextId", networkNextId.toStdString() }, { "version", networkVersion.toStdString() },
                    { "sign", networkSign.toStdString() } };
        network.push_back(row);
    }

    if (local.size() >= network.size())
        return;

    auto last = local.back();
    bool ins = false;
    for (auto el : network)
    {
        if (ins)
            cardFile.append(el.at("id").c_str(), std::stoi(el.at("type").c_str()),
                            std::stoi(el.at("version").c_str()), el.at("sign").c_str());
        if (last["id"] == el["id"])
        {
            ins = true;
        }
    }

    dfsValidate(response.actorId);

#ifdef ECONSOLE
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
#ifdef ECONSOLE
    sender->sendDfsMessage(cfc, Messages::DFSMessage::cardFileChange);
#endif

    QString filePath = QString::fromStdString(CardManager::buildPathForFile(
        cfc.actorId.toStdString(), cfc.fileId.toStdString(), DfsStruct::Type(cfc.type)));

    if (QFile::exists(filePath)) // TODO: root check
        return;

    if (cfc.isEmpty())
    {
        qDebug() << "cardFileChange = empty";
    }
    else
    {
        qDebug() << "cardFileChange =" << cfc.fileId << cfc.type;
    }

    DBConnector dbc((DfsStruct::ROOT_FOOLDER_NAME + "/" + cfc.actorId + "/" + DfsStruct::ACTOR_CARD_FILE
                     + DfsStruct::ACTOR_CARD_FUTURE)
                        .toStdString());
    dbc.createTable(Config::DataStorage::cardTableCreation);

    DBRow row = { { "key", std::to_string(cfc.key) },     { "id", cfc.fileId.toStdString() },
                  { "prevId", cfc.prevId.toStdString() }, { "nextId", cfc.nextId.toStdString() },
                  { "type", std::to_string(cfc.type) },   { "version", std::to_string(cfc.version) },
                  { "sign", cfc.sign.toStdString() } };
    bool res = dbc.insert(Config::DataStorage::cardTableName, row);
    qDebug() << "Save to future" << res;

    std::string file =
        CardManager::buildPathForFile(cfc.actorId.toStdString(), row["id"], DfsStruct::Type(cfc.type));
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

    DBConnector dbc((DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/" + DfsStruct::ACTOR_CARD_FILE
                     + DfsStruct::ACTOR_CARD_FUTURE)
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
    if (path.isEmpty() && data.isEmpty())
    {
        qDebug() << "[DFS Save] Path or data is empty";
        return;
    }
    if (!path.isEmpty() && !QFile::exists(path))
    {
        qDebug() << "[DFS Save] File" << path << "not exists";
        return;
    }

    QByteArray userId = accountControler->getMainActor()->id().toActorId();
    QString dfsPath;
    bool exists = false;

    if (data.left(13) == "encryptfile::")
    {
        QString encryptPath = "tmp/"
            + QString::number(QDateTime::currentSecsSinceEpoch()
                              + QRandomGenerator::global()->bounded(10000));
        Utils::encryptFile(path, encryptPath, data.mid(13));
        dfsPath = buildDfsPath(encryptPath, "", userId, type);
        bool renameRes = QFile::rename(encryptPath, dfsPath);
        qDebug() << "renameRes" << renameRes;
        if (!renameRes)
        {
            qDebug() << QFile(encryptPath).size() << QFile(dfsPath).size();
            if (QFile(encryptPath).size() == QFile(dfsPath).size())
                emit fileDuplicated(dfsPath, path, type);
            QFile::remove(encryptPath);
            return;
        }
    }
    else if (path.isEmpty()) // if !path AND data
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

#ifdef ECLIENT
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
    {
        qDebug() << "Dfs::saveToDFS: Can't append to card file";
        return;
    }

    if (isHaveStoredType(type))
    {
        if (!createStored(dfsPath, userId, type))
        {
            return;
        }
        else
        {
            QByteArray d = data;
            if (data.isEmpty())
            {
                QFile file(dfsPath);
                file.open(QFile::ReadOnly);
                d = file.readAll();
                file.close();
            }

            QString range = QString("0:%1").arg(d.size());

            DistFileSystem::DfsChanges dfsChanges;
            dfsChanges.data << d;
            dfsChanges.range = "sql";
            dfsChanges.userId = accountControler->getMainActor()->id().toActorId();
            QByteArray sType = DfsStruct::toByteArray(type);
            dfsChanges.filePath = dfsPath;
            dfsChanges.changeType = 3;
            dfsChanges.messHash = Utils::calcKeccak(QByteArray::number(
                QRandomGenerator::global()->bounded(50000) + QDateTime::currentMSecsSinceEpoch()));
            dfsChanges.fileVersion = CardManager::dfsVersion(dfsChanges.filePath);
            dfsChanges.sign = accountControler->getMainActor()->key()->sign(dfsChanges.prepareSign());

            bool stored = appendToStored(dfsChanges, true);
            if (!stored)
            {
                qDebug() << "Dfs::saveToDFS: Can't append to stored";
                return;
            }
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
    cardFileChange.version = std::stoi(last["version"]);
    cardFileChange.sign = last["sign"].c_str();
    if (cardFileChange.fileId.isEmpty())
    {
        qDebug() << "empty file id";
        std::exit(1);
    }
    sender->sendDfsMessage(cardFileChange, Messages::DFSMessage::cardFileChange);
    qDebug() << "Send root change" << cardFileChange.fileId << cardFileChange.type;

    emit fileAdded(dfsPath, path, type, userId);
}

bool Dfs::appendToCard(const QString &path, const QByteArray &userId, const DfsStruct::Type &type,
                       bool isFilePath)
{
    QByteArray sign = accountControler->getMainActor()->key()->sign(
        (isFilePath ? CardManager::cutPath(path) : path).toLatin1() + QByteArray::number(type));

    CardFile cardFile(userId);
    if (!cardFile.open())
        return false;

    QString fileId = isFilePath ? CardManager::cutPath(path) : path;
    QString filePath = QString::fromStdString(
        CardManager::buildPathForFile(userId.toStdString(), fileId.toStdString(), DfsStruct::Type(type)));
    int version = CardManager::dfsVersion(filePath);

    bool result = cardFile.append(path.toUtf8(), type, version, sign, isFilePath);
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
                    CardFile cardFile(path.mid(DfsStruct::ROOT_FOOLDER_NAME_MID, 20));
                    if (cardFile.open())
                        cardFile.updateLastCache();
                }
                dfsValidate(path.split("/")[1].toUtf8());
            }
        }
        return;
    }

    if (path.right(DfsStruct::STORED_EXT_SIZE) == DfsStruct::STORED_EXT) // (type == DfsStruct::Type::stored)
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

    if (path.right(DfsStruct::STORED_EXT_SIZE) != DfsStruct::STORED_EXT)
    {
        // TODO: check types
        requestFile(path + DfsStruct::STORED_EXT);
    }

    qDebug() << "File received:" << path;

    QByteArray userId = path.split("/")[1].toUtf8();
    QString fileId = CardManager::cutPath(path);
    QString cardFile = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/root";

    if (QFile::exists(cardFile + DfsStruct::ACTOR_CARD_FUTURE))
    {
        DBConnector rootFuture;
        if (!rootFuture.open((cardFile + DfsStruct::ACTOR_CARD_FUTURE).toStdString()))
        {
            return;
        }

        auto itemFuture = rootFuture.select("SELECT * FROM Items WHERE id = '" + fileId.toStdString() + "'");

        if (!itemFuture.empty())
        {
            rootFuture.deleteRow("Items", { { "id", fileId.toStdString() } });
            rootFuture.close();
            int version = QString::fromStdString(itemFuture[0]["version"]).toInt();
            if (version == 0)
                version = 1;

            CardFile card(userId);
            card.open();
            card.append(fileId, std::stoi(itemFuture[0]["type"]), version,
                        QByteArray::fromStdString(itemFuture[0]["sign"]), false,
                        QString::fromStdString(itemFuture[0]["key"]).toInt());
            auto lastRes = card.last();
            if (lastRes)
            {
                auto lastRow = *lastRes;
                DistFileSystem::CardFileChange cardFileChange;
                cardFileChange.key = std::stoi(lastRow["key"]);
                cardFileChange.actorId = userId;
                cardFileChange.fileId = lastRow["id"].c_str();
                cardFileChange.prevId = lastRow["prevId"].c_str();
                cardFileChange.nextId = lastRow["nextId"].c_str();
                cardFileChange.type = std::stoi(lastRow["type"]);
                cardFileChange.version = std::stoi(lastRow["version"]);
                cardFileChange.sign = lastRow["sign"].c_str();
                sender->sendDfsMessage(cardFileChange, Messages::DFSMessage::cardFileChange);
            }
        }
        else
        {
            rootFuture.close();
        }
    }

    this->dfsValidate(userId);

    bool haveStored = isHaveStoredType(type);
    if (haveStored && path.right(DfsStruct::STORED_EXT_SIZE) == DfsStruct::STORED_EXT)
        emit fileAdded(path.mid(0, path.length() - 7), "network", type,
                       path.mid(DfsStruct::ROOT_FOOLDER_NAME_MID, 20));
    if (!haveStored)
        emit fileAdded(path, "network", type, path.mid(DfsStruct::ROOT_FOOLDER_NAME_MID, 20));
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

    // QString storedPath = filePath + DfsStruct::STORED_EXT;
    // if (QFile::exists(storedPath))
    //     sender->sendFile(storedPath, DfsStruct::Type::Stored, receiver);

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
    QByteArray userId = accountControler->getMainActor()->id().toActorId();
    QByteArray sType = DfsStruct::toByteArray(type);
    QString dfsPath = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/" + sType + "/" + fileName;

    if (!QFile::exists(dfsPath)) // and no stored
        return;

    if (needStored && isHaveStoredType(type))
    {
        if (!createStored(dfsPath, userId, type))
        {
            return;
        }
        else
        {
            QFile file(dfsPath);
            file.open(QFile::ReadOnly);
            QByteArray data = file.readAll();
            file.close();

            // temp
            QString range = QString("0:%1").arg(data.size());
            // DFSMessage::DfsChanges dfsChanges(dfsPath, { data }, range, 3, userId, userId);
            bool card = appendToCard(dfsPath, userId, DfsStruct::Type(static_cast<int>(type) + 100), true);

            DistFileSystem::DfsChanges dfsChanges;
            dfsChanges.data << data;
            dfsChanges.range = "sql";
            dfsChanges.userId = accountControler->getMainActor()->id().toActorId();
            QByteArray sType = DfsStruct::toByteArray(type);
            dfsChanges.filePath = dfsPath;
            dfsChanges.changeType = 3;
            dfsChanges.messHash = Utils::calcKeccak(QByteArray::number(
                QRandomGenerator::global()->bounded(50000) + QDateTime::currentMSecsSinceEpoch()));
            dfsChanges.fileVersion = CardManager::dfsVersion(dfsChanges.filePath);
            dfsChanges.sign = accountControler->getMainActor()->key()->sign(dfsChanges.prepareSign());

            bool stored = appendToStored(dfsChanges, true);

            if (!card)
            {
                qDebug() << "Dfs::saveStaticFile: Can't append to card file";
                return;
            }
            if (!stored)
            {
                qDebug() << "Dfs::saveStaticFile: Can't append to stored";
                return;
            }
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
    cardFileChange.version = std::stoi(last["version"]);
    cardFileChange.sign = last["sign"].c_str();
    if (sender != nullptr)
        sender->sendDfsMessage(cardFileChange, Messages::DFSMessage::cardFileChange);

    emit fileAdded(dfsPath.toLatin1(), fileName, type, userId);
}

void Dfs::editData(QString userId, QString fileName, DfsStruct::Type type, QByteArray data)
{
    DistFileSystem::DfsChanges dfsChanges;
    dfsChanges.userId = accountControler->getMainActor()->id().toActorId();
    dfsChanges.changeType = 3;
    dfsChanges.fileVersion = CardManager::dfsVersion(dfsChanges.filePath);
    int pckg = 0;

    QByteArray sType = DfsStruct::toByteArray(type);
    dfsChanges.filePath = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/" + sType + "/" + fileName;
    QFile file(dfsChanges.filePath);
    qDebug() << "->" << file.open(QFile::ReadOnly);

    QByteArrayList pckgNums;

    while (file.bytesAvailable() > 0)
    {
        auto readed = file.read(DistFileSystem::dataSize);

        QByteArray newDataPart = data.mid(DistFileSystem::dataSize * pckg, DistFileSystem::dataSize);
        // qDebug() << "rea" << readed;
        // qDebug() << "new" << newDataPart;
        // qDebug() << "";
        if (readed != newDataPart)
        {
            pckgNums << QByteArray::number(pckg);
            // dfsChanges.range += " " + QByteArray::number(pckg);
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

    // qDebug() << dfsChanges.range;
    // qDebug() << dfsChanges.data;

    if (applyChanges(dfsChanges))
        sender->sendDfsMessage(dfsChanges, Messages::DFSMessage::changesMessage);
}

void Dfs::editSqlDatabase(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                          QByteArrayList sqlChanges)
{
    DistFileSystem::DfsChanges dfsChanges;
    dfsChanges.data << sqlChanges;
    dfsChanges.range = "sql";
    dfsChanges.userId = accountControler->getMainActor()->id().toActorId();
    QByteArray sType = DfsStruct::toByteArray(type);
    dfsChanges.filePath = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/" + sType + "/" + fileName;
    dfsChanges.changeType = sqlType;
    dfsChanges.messHash = Utils::calcKeccak(
        QByteArray::number(QRandomGenerator::global()->bounded(50000) + QDateTime::currentMSecsSinceEpoch()));
    dfsChanges.fileVersion = CardManager::dfsVersion(dfsChanges.filePath);

    if (applyChanges(dfsChanges))
    {
        sender->sendDfsMessage(dfsChanges, Messages::DFSMessage::changesMessage);
    }
}

bool Dfs::applyChanges(DistFileSystem::DfsChanges &dfsChanges)
{
    if (!QFile::exists(dfsChanges.filePath))
        return false;
    if (dfsChanges.userId != accountControler->getMainActor()->id().toActorId())
    {
        auto actor = actorIndex->getActor(dfsChanges.userId);
        if (actor.empty())
            return false;
        bool verify = actor.key()->verify(dfsChanges.prepareSign(), dfsChanges.sign);
        qDebug() << "DfsChanges Verify applyChanges:" << verify << dfsChanges.prepareSign()
                 << dfsChanges.sign;
    }

    int type = dfsChanges.changeType;
    bool apply = false;

    if (!QFile::exists(dfsChanges.filePath))
    {
        // sender->sendDfsMessage(dfsChanges);
        return false;
    }
    if (!QFile::exists(dfsChanges.filePath + DfsStruct::STORED_EXT))
    {
        // sender->sendDfsMessage(dfsChanges);
        return false;
    }

    DBConnector db;
    if (!db.open((dfsChanges.filePath + DfsStruct::STORED_EXT).toStdString()))
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
        if (appendToStored(dfsChanges, false))
        {
            emit fileChanged(dfsChanges.filePath, DfsStruct::ChangeType(dfsChanges.changeType));

            if (dfsChanges.filePath.contains("chats") && dfsChanges.filePath.contains("msg"))
                emit chatMessage(dfsChanges.userId, dfsChanges.filePath);

            return true;
        }
    }

    return false;
}

void Dfs::applyReplace(QString userId, QString fileName, QString dfsFileName, DfsStruct::Type type)
{
    QByteArray data;
    QFile f(fileName);
    f.open(QFile::ReadOnly);
    data = f.readAll();
    f.close();
    f.remove();

    editData(userId, dfsFileName, type, data);
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
        DBRow row;
        if (data.length() < 3)
        {
            qDebug() << "[applyChangesSql] Delete error. Length < 3:" << data.length();
        }

        for (int i = 1; i != data.length(); i += 2)
            row.insert({ data[i].toStdString(), data[i + 1].toStdString() });

        std::string tableName = data[0].toStdString();
        return db.deleteRow(tableName, row);
    }
    else if (dfsChanges.changeType == DfsStruct::Insert || dfsChanges.changeType == DfsStruct::Update)
    {
        DBRow row;

        for (int i = 1; i < data.length(); i += 2)
            row.insert({ data[i].toStdString(), data[i + 1].toStdString() });

        return dfsChanges.changeType == DfsStruct::Update ? db.replace(data[0].toStdString(), row)
                                                          : db.insert(data[0].toStdString(), row);
    }
    else if (dfsChanges.changeType == DfsStruct::NewColumn)
    {
        QByteArray query;
        if (data.length() >= 3)
        {
            if (data.length() == 3)
                query = "ALTER TABLE " + data[0] + " ADD COLUMN " + data[1] + " " + data[2];
            if (data.length() == 4)
                query += " " + data[3];
            return db.query(query.toStdString());
        }
    }
    else if (dfsChanges.changeType == DfsStruct::RemoveColumn)
    {
    }
    else if (dfsChanges.changeType == DfsStruct::CreateTable)
    {
        if (data.length() == 2)
        {
            QByteArray query = "CREATE TABLE IF NOT EXISTS " + data[0] + " (" + data[1] + ");";
            return db.query(query.toStdString());
        }
    }
    else if (dfsChanges.changeType == DfsStruct::RenameTable)
    {
    }
    else if (dfsChanges.changeType == DfsStruct::DropTable)
    {
        if (data.length() == 1)
        {
            QByteArray query;
            query = "DROP TABLE " + data[0];
            return db.query(query.toStdString());
        }
    }

    // else if (dfsChanges.changeType == DfsStruct::Update)
    // {
    //     // QByteArray query = "UPDATE " + data[0] + "SET ... WHERE " + data[1] + " = '" + data[2] + "';";
    //     return false; // db.update(query.toStdString());
    // }

    return false;
}

DfsStruct::Type Dfs::getFileType(const QString &filePath)
{
    return CardManager::getTypeByName(filePath);
}

bool Dfs::isHaveStoredType(int type)
{
    if (type > 100)
        type -= 100;

    if (type == DfsStruct::Post || type == DfsStruct::Event || type == DfsStruct::Service
        || type == DfsStruct::Contract || type == DfsStruct::Chat || type == DfsStruct::Private)
        return true;

    return false;
}

QStringList Dfs::tmpFiles() const
{
    return m_tmpFiles;
}

void Dfs::dfsSyncUsers(QList<QString> userId, const SocketPair &receiver)
{
    for (QString s : userId)
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
    QByteArray mainActor = accountControler->getMainActor()->id().toActorId();
    QDir acDir(DfsStruct::ROOT_FOOLDER_NAME);
    QStringList acList =
        !myQuickMode ? acDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot) : QStringList { mainActor };
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
    //    QString myCardFile = DfsStruct::ROOT_FOOLDER_NAME + "/" + mainActor + "/" +
    //    DfsStruct::ACTOR_CARD_FILE; QStringList reqCards; QDir acDir(DfsStruct::ROOT_FOOLDER_NAME);
    //    QStringList acList = acDir.entryList(QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
    //    //    int pos = acList.indexOf(mainActor);
    //    //    if (pos != -1)
    //    //        acList.removeAt(pos);
    //    dfsSyncUsers(acList, receiver);
}

bool Dfs::dfsValidate(QByteArray userId)
{
    if (ignoredIds.contains(userId))
    {
        // qDebug() << "dfsValidate ignore" << userId;
        return true;
    }
    if (!actorIndex->hasActor(BigNumber(userId)))
        return false;

    QString cardFile = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/" + DfsStruct::ACTOR_CARD_FILE;

    if (!QFile::exists(cardFile))
        return false;

    DBConnector root;
    DBConnector rootFuture;
    if (!root.open(cardFile.toStdString())
        || !rootFuture.open((cardFile + DfsStruct::ACTOR_CARD_FUTURE).toStdString()))
        return false;

    auto itemsFuture = rootFuture.select("SELECT * FROM Items");
    auto items = root.select("SELECT * FROM Items");
    root.close();
    rootFuture.close();

    items.insert(items.end(), std::make_move_iterator(itemsFuture.begin()),
                 std::make_move_iterator(itemsFuture.end()));
    itemsFuture.clear();

    if (items.empty())
        return true;

    std::string fPath;
    bool flag = true;

    for (DBRow &item : items)
    {
        if (item["id"].empty())
            continue;

        std::string typeStr = item["type"];
        if (typeStr.empty())
            typeStr = "0";
        int type = std::stoi(typeStr);

#ifdef ECLIENT
        if (type == DfsStruct::Type::Files)
            continue;
#endif

        if (myQuickMode && (type != DfsStruct::Type::Service + 100 && type != DfsStruct::Type::Private + 100))
            continue;

        if (item["id"] == "avatar")
            type = 106;

        fPath = CardManager::buildPathForFile(userId.toStdString(), item["id"], DfsStruct::Type(type));
        QFileInfo file(QString::fromStdString(fPath));
        if (!file.exists() || file.size() == 0)
        {
            requestFile(QString::fromStdString(fPath));
            flag = false;
            continue;
        }

        if (isHaveStoredType(type))
        {
            QFileInfo file(QString::fromStdString(fPath) + DfsStruct::STORED_EXT);
            if (!file.exists() || file.size() == 0)
            {
                requestFile(QString::fromStdString(fPath) + DfsStruct::STORED_EXT);
                flag = false;
            }
        }
    }

    return flag;
}

QList<QByteArray> Dfs::dfsValidateAll()
{
    QByteArray mainActor = accountControler->getMainActor()->id().toActorId();
    // QString myCardFile = DfsStruct::ROOT_FOOLDER_NAME + "/" + mainActor + "/" + DfsStruct::ACTOR_CARD_FILE;
    QStringList reqCards;
    QDir acDir(DfsStruct::ROOT_FOOLDER_NAME);
    QStringList acList =
        !myQuickMode ? acDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot) : QStringList { mainActor };
    // int pos = acList.indexOf(mainActor);
    // if (pos != -1)
    //     acList.removeAt(pos);
    QList<QByteArray> res;
    for (QString user : acList)
    {
        bool validated = dfsValidate(user.toUtf8());
        if (!validated)
            res.append(user.toUtf8());

        if (myQuickMode && validated)
        {
            myQuickMode = false;
            dfsValidateAll();
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
    {
        auto actorAccount = actorIndex->getActor(actor).account();
        if (actorAccount == ActorType::Wallet)
        {
            // qDebug() << "add to ignored" << actor;
            ignoredIds << actor;
            continue;
        }

        initDFS(actor);
    }

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
        // qDebug() << "root requested";
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
        if (reqDate + 7000 <= current)
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

void Dfs::requestFileUiHandle(QString filePath)
{
    requestFile(filePath);
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

QString Dfs::buildDfsPath(QString originalFile, QByteArray hash, QByteArray userId, DfsStruct::Type type)
{
    QByteArray sType = DfsStruct::toByteArray(type);
    QString dfsPath = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/" + sType + "/";

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
bool Dfs::appendToStored(DistFileSystem::DfsChanges &dfsChanges, bool init)
{
    DBConnector dbc((dfsChanges.filePath + DfsStruct::STORED_EXT).toStdString());

    if (!init && dfsChanges.userId == accountControler->getMainActor()->id().toActorId())
    {
        auto res = dbc.select("SELECT hash FROM Stored ORDER by _ROWID_ DESC LIMIT 1 ");
        if (!res.size())
            return false;
        dfsChanges.prevHash = QByteArray::fromStdString(res[0]["hash"]);
        dfsChanges.sign = accountControler->getMainActor()->key()->sign(dfsChanges.prepareSign());
    }

    DBRow row = { { "version", std::to_string(dfsChanges.fileVersion) },
                  { "hash", dfsChanges.messHash.toStdString() },
                  { "sign", dfsChanges.sign.toStdString() },
                  { "type", std::to_string(dfsChanges.changeType) },
                  { "userId", dfsChanges.userId.toStdString() },
                  { "range", dfsChanges.range.toStdString() },
                  { "prevHash", dfsChanges.prevHash.toStdString() },
                  { "data", Serialization::serialize(dfsChanges.data, 8).toStdString() } };

    if (init)
        return dbc.insert("Stored", row);

    bool queryRes1 = dbc.insert("Stored", row);

    bool queryRes2 = false;
    if (queryRes1)
    {
        DBConnector db(DfsStruct::ROOT_FOOLDER_NAME.toStdString() + "/"
                       + dfsChanges.filePath.mid(DfsStruct::ROOT_FOOLDER_NAME_MID, 20).toStdString()
                       + "/root");
        std::string version = std::to_string(dfsChanges.fileVersion);

        QString fileId = CardManager::cutPath(dfsChanges.filePath);
        std::string query = "UPDATE " + Config::DataStorage::cardTableName + " SET version = '" + version
            + "' WHERE id = '" + fileId.toStdString() + "' AND version < " + version;
        queryRes2 = db.update(query);
    }

    return queryRes2;
}

void Dfs::updateFromNewStored(QString filePath)
{ // TODO: attach
    qDebug() << "Looking for difference in Stored:" << filePath;
    QString oldStoredPath = filePath;
    QString newStoredPath = filePath + ".new";
    QString userId = filePath.mid(DfsStruct::ROOT_FOOLDER_NAME_MID, 20);
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

            if (type == 3) // TODO: type for file
            {
                QFile file(notStoredNew);

                if (!file.open(QFile::WriteOnly))
                {
                    qDebug() << "-------> Not so good";
                    return;
                }

                // qDebug() << QByteArray::fromStdString(stored.at("data")) << stored.size();
                QByteArray data = QByteArray::fromStdString(stored.at("data"));
                QByteArrayList datas = Serialization::deserialize(data, 8);
                if (datas.isEmpty())
                {
                    qDebug() << "updateFromNewStored error";
                    return;
                }
                file.write(datas[0]);
                file.close();
                continue;
            }

            QByteArray data = QByteArray::fromStdString(stored.at("data"));
            QByteArray range = QByteArray::fromStdString(stored.at("range"));
            QByteArray userId = stored.at("userId").c_str();

            switch (type)
            {
            case DfsStruct::ChangeType::Insert: {
                QByteArrayList list = Serialization::deserialize(data, 8);
                table = list[0];
                DBRow row;
                for (int i = 1; i < list.length(); i += 2)
                    row.insert({ list[i].toStdString(), list[i + 1].toStdString() });
                rows.push_back(row);
                break;
            }
            case DfsStruct::ChangeType::Delete: {
                QByteArrayList list = Serialization::deserialize(data, 8);
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
            return;

        for (const auto &row : rows)
            db.insert(table.toStdString(), row);
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
        emit fileChanged(notStored, DfsStruct::Global); // TODO: push
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
       Serialization::deserialize(QByteArray::fromStdString(el.at("type"))); dfsChanges.range =
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
    QByteArray userId = accountControler->getMainActor()->id().toActorId();
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
    auto actorAccount = actorIndex->getActor(userId).account();
    if (actorAccount == ActorType::Wallet)
    {
        // qDebug() << "initUser add to ignored" << actor;
        ignoredIds << userId.toActorId();
        return;
    }

    initDFS(userId.toActorId());
    // QString cPath =
    //     DfsStruct::ROOT_FOOLDER_NAME + '/' + userId.toActorId() + '/' + DfsStruct::ACTOR_CARD_FILE;
    // if (accountControler->getMainActor() == nullptr)
    //     return;
    //    DFSMessage::dfs_request rqst(cPath, accountControler->getMainActor()->getId().toActorId());
    //    dfsNetManager->send(rqst.serialize());
}

void Dfs::reportFileCompleted(QString filePath, SocketPair receiver)
{
    qDebug() << "[DFS] File" << filePath << "loaded from" << receiver;
    DistFileSystem::DfsRequestFinished fileCompleted;
    fileCompleted.filePath = filePath;
    sender->sendDfsMessage(fileCompleted, Messages::DFSMessage::fileCompleted, receiver,
                           Config::Net::TypeSend::Focused);
}

void Dfs::save(DfsStruct::DfsSave saveType, QString file, QByteArray data, const DfsStruct::Type type)
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

    // qDebug() << "search tmp" << m_reqFiles.length();

    QStringList dataIds = QDir(DfsStruct::ROOT_FOOLDER_NAME).entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &id : dataIds)
    {
        if (ignoredIds.contains(id))
        {
            // qDebug() << "searchTmp ignore" << id;
            continue;
        }

        QString rootPath = DfsStruct::ROOT_FOOLDER_NAME + "/" + id + "/root";
        QFileInfo root(rootPath);
        // QFileInfo rootTmp(rootPath + ".tmp");

        if (actorIndex->companyId != nullptr)
        {
            QByteArray companyId = *actorIndex->companyId;

            if (id == companyId)
                continue;
        }

        if (dfsNetManager->isLoading(rootPath))
            return;

        if (!root.exists() || root.size() == 0)
        {
            requestCardById(id.toLatin1());
        }
    }

    QDirIterator dirIt(DfsStruct::ROOT_FOOLDER_NAME, QDirIterator::Subdirectories);
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
    if (myQuickMode && userId != accountControler->getMainActor()->id().toActorId())
        return;

    if (ignoredIds.contains(userId))
    {
        // qDebug() << "requestCardById ignore" << userId;
        return;
    }

    if (dfsNetManager == nullptr || sender == nullptr)
    {
        qDebug().nospace() << "What's up, Doc? " << (dfsNetManager == nullptr ? "dfsNetManager" : "sender")
                           << " == nullptr";
        return;
    }

    QString fileName = DfsStruct::ROOT_FOOLDER_NAME + "/" + userId + "/root";

    // qDebug() << ">>>>" << fileName << QFile::exists(fileName) << QFile(fileName).size();
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

void Dfs::enableMyQuickMode()
{
    myQuickMode = true;
}

void Dfs::disableMyQuickMode()
{
    myQuickMode = false;
    dfsSyncT();
}
