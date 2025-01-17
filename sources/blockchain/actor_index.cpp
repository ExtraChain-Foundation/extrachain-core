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
#include "network/network_manager.h"

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

std::expected<Actor<KeyPublic>, ActorIndexError> ActorIndex::get_actor(const ActorId &id) {
    if (id.is_zero()) {
        eWarning("[ActorIndex] Error: try get actor with id: {}", id);
        return std::unexpected(ActorIndexError::ZeroActor);
    }

    std::string serialized_actor = this->getById(id).toStdString();
    if (!serialized_actor.empty()) {
        auto actor = Actor<KeyPublic>::fromJson(serialized_actor);
        return actor;
    } else {
        sendGetActorMessage(id);
        eWarning("[ActorIndex] There no actor with id: {}", id);
        return std::unexpected(ActorIndexError::NoActor);
    }
}

bool ActorIndex::validateBlock(const BlockVariant &block) {
    auto signatures = block.signatures();

    for (const auto &[actorId, signature] : signatures) {
        Actor<KeyPublic> actor = this->getActor(actorId);

        if (actor.empty()) {
            eWarning("Can not validate block {}. There no actor {} in local storage", block.getIndex(), actorId);
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
        node->network()->send_message(actor,
                                      MessageType::Actor,
                                      Config::Net::TypeSend::Focused,
                                      MessageStatus::Response,
                                      messageId);
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
        node->network()->send_message(result,
                                      MessageType::ActorAll,
                                      Config::Net::TypeSend::Focused,
                                      MessageStatus::Response,
                                      messageId);
    } else {
        // send empty response
    }
    return;
}

void ActorIndex::getAllActors(ActorId id, bool isUser) {
    Q_UNUSED(isUser)

    if (!node->accountController()->empty()) {
        node->network()->send_message(id,
                                      MessageType::ActorAll,
                                      Config::Net::TypeSend::AllParents,
                                      MessageStatus::Request);

        eLog("[ActorIndex] Get all actors request");
    }
}

void ActorIndex::handleNewAllActors(const std::vector<ActorId> &actors) {
    for (const auto &actor : actors)
        getActor(actor);
}

void ActorIndex::getActorCount(const QByteArray &requestHash, const std::string &messageId) {
    eLog("[ActorIndex] Get actor count response: {}", this->getRecords());

    node->network()->send_message(std::to_string(this->getRecords()),
                                  MessageType::ActorCount,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response);
}

bool ActorIndex::actorExist(const ActorId &actorId) {
    return !getById(actorId).isEmpty();
}

std::string ActorIndex::getFolderPath() const {
    return folderPath;
}

QString ActorIndex::buildFilePath(const ActorId &id) const {
    QByteArray Id = id.toQByteArray(); // id.to_string - std::string

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

std::expected<void, ActorSaveError> ActorIndex::add(const ActorId &id, const QByteArray &data) {
    QString path = buildFilePath(id);
    QFile   file(path);

    if (file.exists()) {
        eLog("[ActorIndex] Can't save file {}: already exist", path);
        return std::unexpected(ActorSaveError::AlreadyExists);
    }

    if (!file.open(QIODevice::WriteOnly)) {
        eLog("[ActorIndex] Can't save file {}: not opened", path);
        return std::unexpected(ActorSaveError::NotOpened);
    }

    eLog("[ActorIndex] Saving the file: {}", path);
    file.write(data);
    file.flush();
    file.close();
    return {};
}

void ActorIndex::sendGetActorMessage(const ActorId &actorId) {
    if (actorId.is_zero()) {
        eFatal("Can't get actor by zero id");
    }

    node->network()->send_message(actorId.to_string(),
                                  MessageType::Actor,
                                  Config::Net::TypeSend::AllParents,
                                  MessageStatus::Request);
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

std::expected<void, ActorSaveError> ActorIndex::store_new_actor(const Actor<KeyPublic> &actor) {
    auto result = this->save_actor(actor);
    node->network()->send_message(actor, MessageType::NewActor, Config::Net::TypeSend::Broadcast);
    return result;
}

std::expected<void, ActorSaveError> ActorIndex::save_actor(const Actor<KeyPublic> &actor) {
    auto result = this->add(actor.id(), actor.toJson());

    if (!result.has_value()) {
        return std::unexpected(result.error());
    }

    bool res = save_actor_index(actor);
    if (!res) {
        return std::unexpected(ActorSaveError::Undefined);
    }

    emit newActorSaved(actor.id());
    return {};
}

bool ActorIndex::save_actor_index(const Actor<KeyPublic> &actor) {
    this->records++;

    DbConnector db(folderPath + "actors");
    if (!db.open()) {
        return false;
    }

    bool dbInsert = db.insert(Config::DataStorage::actorsTable,
                              { { "id", actor.id().to_string() }, { "type", std::to_string(int(actor.type())) } });
    if (!dbInsert)
        eFatal("db actor insert error");

    return true;
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
