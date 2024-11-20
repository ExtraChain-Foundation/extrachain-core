/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#include "blockchain/actor_index.h"

#include "dfs/dfs_controller.h"

ActorId ActorIndex::firstId() {
    return m_firstId;
}

ActorIndex::ActorIndex(ExtraChainNode *node)
    : QObject(node)
    , node(node) {
    DbConnector db(folderPath + "actors");
    bool        isDbOpen   = db.open();
    bool        isDbCreate = db.create_table(Config::DataStorage::actorsTableCreate);

    if (!isDbOpen || !isDbCreate) {
        eFatal("db for actors (open: {}, create: {})", isDbOpen, isDbCreate);
    }

    records = db.count("Actors");
    eLog("[ActorIndex] Count: {}", records);
}

Actor<KeyPublic> ActorIndex::getActor(const ActorId &id) {
    if (id.is_zero()) {
        eWarning("[ActorIndex] Error: try get actor with id: {}", id);
        return Actor<KeyPublic>();
    }

    QByteArray serializedActor = this->getById(id);
    if (!serializedActor.isEmpty()) {
        auto actor = Actor<KeyPublic>::fromJson(serializedActor);
        return actor;
    } else {
        sendGetActorMessage(id);
        eWarning("[ActorIndex] There no actor with id: {}", id);
        return Actor<KeyPublic>();
    }
}

bool ActorIndex::validateBlock(const BlockVariant &block) {
    auto signatures = block.signatures();

    for (const auto &[actorId, signature] : signatures) {
        Actor<KeyPublic> actor = this->getActor(actorId);

        if (actor.empty()) {
            eWarning(
                "Can not validate block {}. There no actor {} in local storage",
                block.getIndex(),
                actorId);
            continue;
        }

        if (!block.verify(actor))
            return false;
    }

    return true;
}

bool ActorIndex::validateTx(const Transaction &tx) {
    Actor<KeyPublic> actor = this->getActor(tx.approver());
    if (actor.empty()) {
        eWarning("Can not validate tx {}. There no actor {} in local storage", tx.hash(), tx.approver());
        return false;
    }
    return tx.verify(actor);
}

void ActorIndex::handleGetActor(const ActorId &actorId, const std::string &messageId) {
    // receive id
    // create response message
    if (actorId.is_zero())
        eFatal("handleGetActor: empty actor");
    Actor<KeyPublic> actor = getActor(actorId);
    if (!actor.empty()) {
        node->network()->send_message(
            actor,
            MessageType::Actor,
            MessageStatus::Response,
            messageId,
            Config::Net::TypeSend::Focused);
    } else {
        sendGetActorMessage(actorId);
    }
}

void ActorIndex::handleGetAllActor(const ActorId &ignoredActorId, const std::string &messageId) {
    if (node->accountController()->empty())
        return;

    auto result = allActors();
    result.erase(std::remove(result.begin(), result.end(), ignoredActorId), result.end());
    if (!result.empty()) {
        node->network()->send_message(
            result,
            MessageType::ActorAll,
            MessageStatus::Response,
            messageId,
            Config::Net::TypeSend::Focused);
    } else {
        // send empty response
    }
    return;
}

void ActorIndex::getAllActors(ActorId id, bool isUser) {
    Q_UNUSED(isUser)

    if (!node->accountController()->empty()) {
        node->network()->send_message(id, MessageType::ActorAll, MessageStatus::Request);

        eLog("[ActorIndex] Get all actors request");
    }
}

int ActorIndex::handleNewActor(Actor<KeyPublic> actor) {
    switch (addActor(actor)) {
    case 0: {
        eLog("[ActorIndex] New actor {} is successfully saved", actor);
        return Errors::FILE_NOT_EXISTS;
    }
    case Errors::FILE_ALREADY_EXISTS: {
        eLog("[ActorIndex] New actor {} can't be added: it is already in storage", actor);
        return Errors::FILE_ALREADY_EXISTS;
    }
    case Errors::FILE_IS_NOT_OPENED: {
        eWarning("[ActorIndex] Error: new actor {} is not saved", actor);
        return Errors::FILE_IS_NOT_OPENED;
    }
    default: {
        eWarning("[ActorIndex] Error: unexpected return type");
        return Errors::UNDEFINED;
    }
    }
    return Errors::UNDEFINED;
}

void ActorIndex::handleNewAllActors(const std::vector<ActorId> &actors) {
    for (const auto &actor : actors)
        getActor(actor);
}

void ActorIndex::getActorCount(const QByteArray &requestHash, const std::string &messageId) {
    eLog("[ActorIndex] Get actor count response: {}", this->getRecords());

    node->network()->send_message(
        std::to_string(this->getRecords()),
        MessageType::ActorCount,
        MessageStatus::Response);
}

bool ActorIndex::actorExist(const ActorId &actorId) {
    return !getById(actorId).isEmpty();
}

std::string ActorIndex::getFolderPath() const {
    return folderPath;
}

QString ActorIndex::buildFilePath(const ActorId &id) const {
    QByteArray Id = id.toQByteArray();

    QByteArray section      = Id.right(SECTION_NAME_SIZE);
    QString    pathToFolder = QString::fromStdString(folderPath) + section;

    QDir dir(pathToFolder);
    if (!dir.exists()) {
        eLog("[ActorIndex] Creating dir: {}", pathToFolder);
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + "/" + Id;
}

std::string ActorIndex::actorPath(const ActorId &id) const {
    const std::string &idStd = id.to_string();
    return folderPath + idStd.substr(idStd.length() - SECTION_NAME_SIZE) + '/' + idStd;
}

void ActorIndex::setFirstId(const ActorId &value) {
    if (!m_firstId.is_zero()) {
        if (firstId() != value) {
            eFatal("Another FirstId: {} != {}", firstId(), value);
        }
        return;
    }

    eLog("[ActorIndex] Save first id: {}", value);
    m_firstId = value;
}

std::size_t ActorIndex::getRecords() const {
    return records;
}

int ActorIndex::add(const ActorId &id, const QByteArray &data) {
    // if (id <= 1000)
    //     eFatal("Try to add actor with id {}", id);

    QString path = buildFilePath(id);
    QFile   file(path);
    eLog("[ActorIndex] Saving the file: {}", path);

    if (file.exists()) {
        eLog("[ActorIndex] Can't save the file {} (file already exits)", path);
        return Errors::FILE_ALREADY_EXISTS;
    }

    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.flush();
        file.close();

        return 0;
    }

    eLog("[ActorIndex] Can't save the file {} (file is not opened)", path);
    return Errors::FILE_IS_NOT_OPENED;
}

void ActorIndex::sendGetActorMessage(const ActorId &actorId) {
    if (actorId.is_zero()) {
        eFatal("Can't get actor by zero id");
    }

    node->network()->send_message(actorId.to_string(), MessageType::Actor, MessageStatus::Request);
}

QByteArray ActorIndex::getById(const ActorId &id) const {
    QString filePath = QString::fromStdString(actorPath(id));
    QFile   file(filePath);
    if (!file.exists()) {
        eLog("[ActorIndex] File with path {} not found", filePath);
        return QByteArray();
    }
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();
    file.close();
    return data;
}

int ActorIndex::addActor(const Actor<KeyPublic> &actor) {
    int  result  = this->add(actor.id(), actor.toJson());
    auto actorId = actor.id().to_string();

    if (result != Errors::FILE_ALREADY_EXISTS && result != Errors::FILE_IS_NOT_OPENED) {
        this->records++;
        DbConnector db(folderPath + "actors");
        db.open();
        bool dbInsert = db.insert(
            Config::DataStorage::actorsTable,
            { { "id", actorId }, { "type", std::to_string(int(actor.type())) } });
        if (!dbInsert)
            eFatal("db actor insert error");

        node->dfs()->initializeActor(actor.id());

        eLog("[ActorIndex] Actor {} was added", actor.id());
        node->network()->send_message(actor, MessageType::NewActor);
    }

    return result;
}

std::vector<ActorId> ActorIndex::allActors() {
    std::vector<ActorId> result;

    DbConnector db(folderPath + "actors");
    db.open();
    auto actors = db.select("SELECT id FROM Actors");
    for (auto &actor : actors) {
        result.push_back(ActorId(actor["id"]));
    }

    return result;
}
