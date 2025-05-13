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

#include <QDir>

#include "network/network_manager.h"

ActorId ActorIndex::network_id() {
    return network_id_;
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

std::expected<Actor<KeyPublic>, ActorIndexError> ActorIndex::get_actor(const ActorId &id, ActorGetType get_type) {
    if (id.is_zero()) {
        eWarning("[ActorIndex] Error: try get actor with id: {}", id);
        return std::unexpected(ActorIndexError::ZeroActor);
    }

    std::string serialized_actor = this->getById(id).toStdString();
    if (!serialized_actor.empty()) {
        auto actor = Actor<KeyPublic>::fromJson(serialized_actor);
        return actor;
    } else {
        if (get_type == ActorGetType::Request) {
            sendGetActorMessage(id);
        }

        // eWarning("[ActorIndex] There no actor with id: {}", id);
        return std::unexpected(ActorIndexError::NoActor);
    }
}

void ActorIndex::network_actor_request(const ActorId &actorId, const Responder &responder) {
    // receive id
    // create response message
    if (actorId.is_zero())
        eFatal("handleGetActor: empty actor");

    auto actor_result = this->get_actor(actorId, ActorGetType::NoRequest);
    if (!actor_result.has_value()) {
        return;
    }

    auto actor = actor_result.value();
    if (!actor.empty()) {
        responder.send_response(actor, MessageType::Actor, SendMode::Focused, MessageStatus::Response);
    } else {
        sendGetActorMessage(actorId);
    }
}

void ActorIndex::network_actors_all_request(const ActorId &ignoredActorId, const Responder &responder) {
    if (node->accountController()->empty())
        return;

    auto result = allActors();
    result.erase(std::remove(result.begin(), result.end(), ignoredActorId), result.end());
    if (!result.empty()) {
        responder.send_response(result, MessageType::ActorAll, SendMode::Focused, MessageStatus::Response);
    } else {
        // send empty response
    }
    return;
}

void ActorIndex::getAllActors(ActorId id, bool isUser) {
    Q_UNUSED(isUser)

    if (!node->accountController()->empty()) {
        node->network()->send_message(id, MessageType::ActorAll, SendMode::Neighbours, MessageStatus::Request);

        eLog("[ActorIndex] Get all actors request");
    }
}

void ActorIndex::network_actors_all_response(const std::vector<ActorId> &actors, const Responder &responder) {
    std::set<ActorId> needed_actors;

    for (const auto &actor_id : actors) {
        auto actor_result = this->get_actor(actor_id, ActorGetType::NoRequest);
        if (actor_result.has_value()) {
            continue;
        }

        needed_actors.insert(actor_id);
    }

    if (needed_actors.empty()) {
        return;
    }

    responder.with_new_message_id().send_response(needed_actors,
                                                  MessageType::Actors,
                                                  SendMode::Neighbours,
                                                  MessageStatus::Request);
}

void ActorIndex::network_actors_request(const std::set<ActorId> &actors, const Responder &responder) {
    std::vector<Actor<KeyPublic>> req_actors;

    for (const auto &actor_id : actors) {
        auto actor_result = this->get_actor(actor_id, ActorGetType::NoRequest);
        if (!actor_result.has_value()) {
            continue;
        }

        req_actors.push_back(actor_result.value());
    }

    if (req_actors.empty()) {
        return;
    }

    responder.send_response(req_actors, MessageType::Actors, SendMode::Neighbours, MessageStatus::Response);
}

void ActorIndex::network_actors_response(const std::vector<Actor<KeyPublic>> &actors) {
    for (const auto &actor : actors) {
        this->save_actor(actor);
    }
}

void ActorIndex::send_system_actor(const Responder &responder) {
    // auto system_actor = node->accountController()->system_actor().to_public();
    const auto &actors = node->accountController()->currentProfile().actors();
    for (const auto &actor : actors) {
        responder.send_response(actor.to_public(), MessageType::Actor, SendMode::Focused, MessageStatus::Response);
    }
}

void ActorIndex::getActorCount(const QByteArray &requestHash, const Responder &responder) {
    eLog("[ActorIndex] Get actor count response: {}", this->getRecords());

    responder.send_response(std::to_string(this->getRecords()),
                            MessageType::ActorCount,
                            SendMode::Focused,
                            MessageStatus::Response);
}

bool ActorIndex::exists(const ActorId &actor_id) {
    auto actor = this->get_actor(actor_id, ActorGetType::NoRequest);
    return actor.has_value();
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
        // eLog("[ActorIndex] Creating dir: {}", pathToFolder);
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + "/" + Id;
}

std::string ActorIndex::actorPath(const ActorId &id) const {
    const std::string &idStd = id.to_string();
    return folderPath + idStd.substr(idStd.length() - SECTION_NAME_SIZE) + '/' + idStd;
}

void ActorIndex::set_network_id(const ActorId &value) {
    if (!network_id_.is_zero()) {
        if (network_id() != value) {
            eFatal("Another network id: {} != {}", network_id(), value);
        }
        return;
    }

    eLog("[ActorIndex] Save network id: {}", value);
    network_id_ = value;
}

std::size_t ActorIndex::getRecords() const {
    return records;
}

std::expected<void, ActorSaveError> ActorIndex::add(const ActorId &id, const QByteArray &data) {
    QString path = buildFilePath(id);
    QFile   file(path);

    if (file.exists()) {
        // eLog("[ActorIndex] Can't save file {}: already exist", path);
        return std::unexpected(ActorSaveError::AlreadyExists);
    }

    if (!file.open(QIODevice::WriteOnly)) {
        eLog("[ActorIndex] Can't save file {}: not opened", path);
        return std::unexpected(ActorSaveError::NotOpened);
    }

    // eLog("[ActorIndex] Saving the file: {}", path);
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
                                  SendMode::Neighbours,
                                  MessageStatus::Request);
}

QByteArray ActorIndex::getById(const ActorId &id) const {
    QString filePath = QString::fromStdString(actorPath(id));
    QFile   file(filePath);
    if (!file.exists()) {
        // eLog("[ActorIndex] File with path {} not found", filePath);
        return QByteArray();
    }
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();
    file.close();
    return data;
}

std::expected<void, ActorSaveError> ActorIndex::store_new_actor(const Actor<KeyPublic> &actor) {
    auto result = this->save_actor(actor);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }

    emit newActorSaved(actor.id());
    node->network()->send_broadcast(actor, MessageType::NewActor);
    return result;
}

std::expected<void, ActorSaveError> ActorIndex::network_store_new_actor(const Actor<KeyPublic> &actor) {
    auto result = this->save_actor(actor);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }

    emit newActorSaved(actor.id());
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

    emit actorSaved(actor.id());
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
    if (!dbInsert) {
        eCritical("db actor insert error");
        return false;
    }

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
