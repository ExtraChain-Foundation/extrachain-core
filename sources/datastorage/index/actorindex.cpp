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

#include "datastorage/index/actorindex.h"

#include "datastorage/dfs/dfs_controller.h"

ActorId ActorIndex::firstId() {
    return m_firstId;
}

ActorIndex::ActorIndex(ExtraChainNode &node)
    : node(node) {
    DBConnector db(folderPath + "actors");
    bool isDbOpen = db.open();
    bool isDbCreate = db.createTable(Config::DataStorage::actorsTableCreate);

    if (!isDbOpen || !isDbCreate)
        qFatal("%s",
               QString("db for actors (open: %1, create: %2)").arg(isDbOpen, isDbCreate).toLatin1().data());

    records = db.count("Actors");
    qDebug() << "[ActorIndex] Count:" << records;
}
Actor<KeyPublic> ActorIndex::getActor(const ActorId &id) {
    if (id.isEmpty()) {
        qDebug() << "[ActorIndex] Error: try get actor with id =" << id;
        return Actor<KeyPublic>();
    }

    QByteArray serializedActor = this->getById(id);
    if (!serializedActor.isEmpty()) {
        auto actor = Actor<KeyPublic>::fromJson(serializedActor);
        return actor;
    } else {
        sendGetActorMessage(id);
        qDebug() << "[ActorIndex] There no actor with id:" << id;
        return Actor<KeyPublic>();
    }
}

bool ActorIndex::validateBlock(const Block &block) {
    Actor<KeyPublic> actor = this->getActor(block.getApprover());
    if (actor.empty()) {
        qWarning() << "Can not validate block" << block.getIndex() << ": There no actor"
                   << block.getApprover() << " in local storage";
        return false;
    }
    return block.verify(actor);
}

bool ActorIndex::validateTx(const Transaction &tx) {
    Actor<KeyPublic> actor = this->getActor(tx.getApprover());
    if (actor.empty()) {
        qWarning() << "Can not validate tx" << tx.getHash() << ": There no actor" << tx.getApprover()
                   << " in local storage";
        return false;
    }
    return tx.verify(actor);
}

void ActorIndex::handleGetActor(const ActorId &actorId, const std::string &messageId) {
    // receive id
    // create response message
    if (actorId.isEmpty())
        qFatal("handleGetActor: empty actor");
    Actor<KeyPublic> actor = getActor(actorId);
    if (!actor.empty()) {
        node.network()->send_message(actor, MessageType::Actor, MessageStatus::Response, messageId,
                                     Config::Net::TypeSend::Focused);
    } else {
        sendGetActorMessage(actorId);
    }
}

void ActorIndex::handleGetAllActor(const ActorId &ignoredActorId, const std::string &messageId) {
    if (node.accountController()->count() == 0)
        return;

    std::vector<std::string> result = allActorsStd();
    result.erase(std::remove(result.begin(), result.end(), ignoredActorId), result.end());
    if (!result.empty()) {
        node.network()->send_message(result, MessageType::ActorAll, MessageStatus::Response, messageId,
                                     Config::Net::TypeSend::Focused);
    } else {
        // send empty response
    }
    return;
}

void ActorIndex::getAllActors(ActorId id, bool isUser) {
    Q_UNUSED(isUser)

    if (node.accountController()->count() > 0) {
        node.network()->send_message(id, MessageType::ActorAll, MessageStatus::Request);

        qDebug() << "[ActorIndex] Get all actors request";
    }
}

void ActorIndex::handleNewActor(Actor<KeyPublic> actor) {
    switch (addActor(actor)) {
    case 0:
        qDebug() << "[ActorIndex] New actor" << actor << "is successfully saved";
        break;
    case Errors::FILE_ALREADY_EXISTS:
        qDebug() << "[ActorIndex] New actor" << actor << "can't be added: it is already in storage";
        break;
    case Errors::FILE_IS_NOT_OPENED:
        qWarning() << "[ActorIndex] Error: new actor" << actor << "is not saved";
        break;
    default:
        qWarning() << "[ActorIndex] Error: unexpected return type";
    }
}

void ActorIndex::handleNewAllActors(const std::vector<std::string> &actors) {
    for (const std::string &actor : actors)
        getActor(actor);
}

void ActorIndex::getActorCount(const QByteArray &requestHash, const std::string &messageId) {
    qDebug() << "[ActorIndex] Get actor count response:" << this->getRecords();

    node.network()->send_message(std::to_string(this->getRecords()), MessageType::ActorCount,
                                 MessageStatus::Response);
}

bool ActorIndex::actorExist(const ActorId &actorId) {
    return !getById(actorId).isEmpty();
}

std::string ActorIndex::getFolderPath() const {
    return folderPath;
}

QString ActorIndex::buildFilePath(const ActorId &id) const {
    QByteArray Id = id.toByteArray();

    QByteArray section = Id.right(SECTION_NAME_SIZE);
    QString pathToFolder = QString::fromStdString(folderPath) + section;

    QDir dir(pathToFolder);
    if (!dir.exists()) {
        qDebug() << "[ActorIndex] Creating dir:" << pathToFolder;
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + "/" + Id;
}

std::string ActorIndex::actorPath(const ActorId &id) const {
    const std::string &idStd = id.toStdString();
    return folderPath + idStd.substr(idStd.length() - SECTION_NAME_SIZE) + '/' + idStd;
}

void ActorIndex::setFirstId(const ActorId &value) {
    if (!m_firstId.isEmpty()) {
        if (firstId() != value)
            qFatal("Another FirstId");
        return;
    }

    qDebug() << "[ActorIndex] Save first id:" << value;
    m_firstId = value;
}

qint64 ActorIndex::getRecords() const {
    return records;
}

int ActorIndex::add(const ActorId &id, const QByteArray &data) {
    // if (id <= 1000)
    //     qFatal("Try to add actor with id %s", id.toByteArray().constData());

    QString path = buildFilePath(id);
    QFile file(path);
    qDebug() << "[ActorIndex] Saving the file:" << path;

    if (file.exists()) {
        qDebug() << "[ActorIndex] Can't save the file" << path << "(file already exits)";
        return Errors::FILE_ALREADY_EXISTS;
    }

    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.flush();
        file.close();

        return 0;
    }

    qDebug() << "[ActorIndex] Can't save the file" << path << "(file is not opened)";
    return Errors::FILE_IS_NOT_OPENED;
}

void ActorIndex::sendGetActorMessage(const ActorId &actorId) {
    if (actorId.isEmpty()) {
        qFatal("Can't get actor by empty id");
    }

    node.network()->send_message(actorId.toStdString(), MessageType::Actor, MessageStatus::Request);
}

QByteArray ActorIndex::getById(const ActorId &id) const {
    QString filePath = QString::fromStdString(actorPath(id));
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "[ActorIndex] File with path" << filePath << "not found";
        return QByteArray();
    }
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();
    file.close();
    return data;
}

int ActorIndex::addActor(const Actor<KeyPublic> &actor) {
    int result = this->add(actor.id(), actor.toJson());
    auto actorId = actor.id().toStdString();

    if (result != Errors::FILE_ALREADY_EXISTS && result != Errors::FILE_IS_NOT_OPENED) {
        this->records++;
        DBConnector db(folderPath + "actors");
        db.open();
        bool dbInsert = db.insert(Config::DataStorage::actorsTable,
                                  { { "id", actorId }, { "type", std::to_string(int(actor.type())) } });
        if (!dbInsert)
            qFatal("db actor insert error");

        node.dfs()->initializeActor(actor.id());

        qDebug() << "[ActorIndex] Actor" << actor.id() << "was added";
        node.network()->send_message(actor, MessageType::NewActor);
    }

    return result;
}

QByteArrayList ActorIndex::allActors() {
    QByteArrayList result;

    DBConnector db(folderPath + "actors");
    db.open();
    auto actors = db.select("SELECT id FROM Actors");
    for (auto &actor : actors) {
        result << actor["id"].data();
    }

    return result;
}

std::vector<std::string> ActorIndex::allActorsStd() {
    std::vector<std::string> result;

    DBConnector db(folderPath + "actors");
    db.open();
    auto actors = db.select("SELECT id FROM Actors");
    for (auto &actor : actors) {
        result.push_back(actor["id"]);
    }

    return result;
}
