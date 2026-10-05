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

#include "chain/actor_index.h"

#include <QDir>
#include <QScopeGuard>
#include <QTemporaryFile>
#include <QThread>
#include <sqlite3.h>

#include "network/network_manager.h"
#include "utils/thread_pool_boost.h"

namespace {
const auto actorDatabaseLock = std::make_shared<std::recursive_mutex>();
}

ActorId ActorIndex::network_id() {
    /*
    if (network_id_.is_zero()) {
        QFile nid(".network_id");
        if (!nid.exists()) {
            return network_id_;
        }

        nid.open(QFile::ReadOnly);
        auto bytes = nid.readAll().toStdString();
        auto actor_id = ActorId::create(bytes);
        if (!actor_id.has_value()) {
            return network_id_;
        }
        return actor_id.value();
    }
    */

    return network_id_;
}

ActorIndex::ActorIndex(ExtraChainNode *node)
    : QObject(node)
    , node(node) {
    DbConnector db(folder_path_ + "actors", DbConnectorType::Regular, actorDatabaseLock);
    bool        isDbOpen   = db.open();
    bool        isDbCreate = db.create_table(Config::DataStorage::actorsTableCreate);

    if (!isDbOpen || !isDbCreate) {
        eFatal("db for actors (open: {}, create: {})", isDbOpen, isDbCreate);
    }

    records_ = db.count("Actors");
    eLog("[ActorIndex] Count: {}", records_);
    db.close();

    if (records_ > 0) {
        synch_.set_actors(read_all_actors_ids());
    }
}

Actor<KeyPublic> ActorIndex::read_actor_old(const ActorId &id) {
    if (id.is_zero()) {
        eWarning("[ActorIndex] Error: try get actor with id: {}", id);
        return Actor<KeyPublic>();
    }

    QByteArray serializedActor = this->read_by_id(id);
    if (!serializedActor.isEmpty()) {
        auto actor = Actor<KeyPublic>::fromJson(serializedActor);
        return actor;
    } else {
        this->send_get_actor_message(id);
        eWarning("[ActorIndex] There no actor with id: {}", id);
        return Actor<KeyPublic>();
    }
}

std::expected<Actor<KeyPublic>, ActorIndexError> ActorIndex::read_actor(const ActorId &id, ActorGetType get_type) {
    if (id.is_zero()) {
        eWarning("[ActorIndex] Error: try get actor with id: {}", id);
        return std::unexpected(ActorIndexError::ZeroActor);
    }

    std::string serialized_actor = this->read_by_id(id).toStdString();
    if (!serialized_actor.empty()) {
        auto actor = Actor<KeyPublic>::fromJson(serialized_actor);
        return actor;
    } else {
        if (get_type == ActorGetType::Request) {
            this->send_get_actor_message(id);
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

    auto actor_result = this->read_actor(actorId, ActorGetType::NoRequest);
    if (!actor_result.has_value()) {
        return;
    }

    auto actor = actor_result.value();
    if (!actor.empty()) {
        responder.send_response(actor, MessageType::Actor, SendMode::Focused, MessageStatus::Response);
    } else {
        this->send_get_actor_message(actorId);
    }
}

void ActorIndex::network_actors_request(const std::set<ActorId> &actors, const Responder &responder) {
    std::vector<Actor<KeyPublic>> req_actors;

    for (const auto &actor_id : actors) {
        auto actor_result = this->read_actor(actor_id, ActorGetType::NoRequest);
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
    if (!sync_first_done_) {
        for (const auto &actor : actors) {
            auto id              = actor.id().to_string();
            actors_todo_map_[id] = actor;
        }

        // eLog("[ActorIndex] ---> {} {}", synch_count, actors_todo_map_.size());
        if (synch_count_
            <= std::max(actors_todo_map_.size() + std::size_t(records_), actors_todo_map_.size()) + 15) {
            if (!this->save_actors().has_value()) {
                eWarning("[ActorIndex] Initial actor persistence failed");
                return;
            }
            sync_first_done_ = true;
            node->account_controller()->dogenerate();
            emit this->firstSyncEnded();
        }
    } else {
        if (node_enabled.load() && !this->save_actors(actors).has_value()) {
            eWarning("[ActorIndex] Actor response persistence failed");
        }
    }
}

void ActorIndex::send_system_actor(const Responder &responder) {
    // auto system_actor = node->account_controller()->system_actor().to_public();
    const auto &actors = node->account_controller()->current_profile().actors();
    for (const auto &actor : actors) {
        responder.send_response(actor.to_public(), MessageType::Actor, SendMode::Focused, MessageStatus::Response);
    }
}

void ActorIndex::request_actors_hash(const Responder &responder) {
    // TIMER_START(request_actors_hash)
    std::vector<uint8_t> sync_request = synch_.create_sync_request();
    // TIMER_END(request_actors_hash)

    // if (records <= 100) {
    //     emit firstSyncStarted();
    // }

    if (!sync_first_done_) {
        emit this->firstSyncStarted();
    }

    responder.with_new_message_id().send_response(std::pair { records_, sync_request },
                                                  MessageType::ActorsHash,
                                                  SendMode::Focused,
                                                  MessageStatus::Request);
}

void ActorIndex::network_actors_hash_request(std::uint64_t               count,
                                             const std::vector<uint8_t> &bits,
                                             const Responder            &responder) {
    // TIMER_START(network_actors_hash_request)
    synch_count_                   = std::max(synch_count_, count);
    std::vector<ActorId> actor_ids = synch_.process_sync_request(bits);
    // TIMER_END(network_actors_hash_request)

    eLog("[ActorIndex] Diff size: {}, need: {}, local: {}", actor_ids.size(), count, records_);

    if (records_ + 1 >= count && actors_todo_map_.empty()) {
        if (!sync_first_done_) {
            emit this->firstSyncEnded();
        }

        sync_first_done_ = true;
        node->account_controller()->dogenerate();
    }

    auto r = responder;
    ThreadPoolBoost::instance()->post([this, responder = r, actor_ids = std::move(actor_ids)] {
        std::vector<Actor<KeyPublic>> actors;
        auto                          min_size = actor_ids.size() > 100 ? 100 : actor_ids.size();
        actors.reserve(min_size);

        for (const auto &actor_id : actor_ids) {
            auto actor = read_actor(actor_id);
            if (!actor.has_value()) {
                continue;
            }

            actors.push_back(actor.value());

            if (actors.size() > 99) {
                // eLog("[ActorIndex] Send {} actors", actors.size());
                responder.with_new_message_id().send_response(actors,
                                                              MessageType::Actors,
                                                              SendMode::Focused,
                                                              MessageStatus::Response);
                actors.clear();
                QThread::msleep(2);
            }
        }

        if (actors.empty()) {
            return;
        }

        // eLog("[ActorIndex] Send {} actors", actors.size());
        responder.with_new_message_id().send_response(actors,
                                                      MessageType::Actors,
                                                      SendMode::Focused,
                                                      MessageStatus::Response);
    });
}

bool ActorIndex::exists(const ActorId &actor_id) {
    auto actor = this->read_actor(actor_id, ActorGetType::NoRequest);
    return actor.has_value();
}

std::string ActorIndex::folder_path() const {
    return folder_path_;
}

QString ActorIndex::build_file_path(const ActorId &id) const {
    QByteArray Id = id.toQByteArray(); // id.to_string - std::string

    QByteArray section      = Id.right(SECTION_NAME_SIZE);
    QString    pathToFolder = QString::fromStdString(folder_path_) + section;

    QDir dir(pathToFolder);
    if (!dir.exists()) {
        // eLog("[ActorIndex] Creating dir: {}", pathToFolder);
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + "/" + Id;
}

std::string ActorIndex::build_actor_path(const ActorId &id) const {
    const std::string &idStd = id.to_string();
    return folder_path_ + idStd.substr(idStd.length() - SECTION_NAME_SIZE) + '/' + idStd;
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

std::size_t ActorIndex::records() const {
    return records_;
}

std::expected<void, ActorSaveError> ActorIndex::add(const ActorId &id, const QByteArray &data) {
    QString path = build_file_path(id);

    if (QFile::exists(path)) {
        return std::unexpected(ActorSaveError::AlreadyExists);
    }

    QTemporaryFile file(path + ".XXXXXX");
    if (!file.open()) {
        eLog("[ActorIndex] Can't save file {}: not opened", path);
        return std::unexpected(ActorSaveError::NotOpened);
    }

    if (file.write(data) != data.size() || !file.flush()) {
        return std::unexpected(ActorSaveError::Undefined);
    }
    file.close();
    if (file.error() != QFileDevice::NoError || !file.rename(path)) {
        return std::unexpected(ActorSaveError::Undefined);
    }
    file.setAutoRemove(false);
    return {};
}

void ActorIndex::send_get_actor_message(const ActorId &actorId) {
    if (actorId.is_zero()) {
        eFatal("Can't get actor by zero id");
    }

    node->network()->send_message(actorId.to_string(),
                                  MessageType::Actor,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);
}

QByteArray ActorIndex::read_by_id(const ActorId &id) const {
    QString filePath = QString::fromStdString(build_actor_path(id));
    QFile   file(filePath);
    if (!file.exists()) {
        // eLog("[ActorIndex] File with path {} not found", filePath);
        return QByteArray();
    }
    bool is_open = file.open(QIODevice::ReadOnly);
    if (!is_open) {
        return QByteArray();
    }
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

    if (node->network()->is_active_connection_exists()) {
        node->network()->send_broadcast(actor, MessageType::NewActor);
    } else {
        node->actors_broadcast_.push_back(actor);
    }

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
    auto result = save_actor_batch({ &actor });
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    if (result->empty()) {
        return std::unexpected(ActorSaveError::AlreadyExists);
    }
    emit actorSaved(actor.id());
    return {};
}

std::expected<void, ActorSaveError> ActorIndex::save_actors(const std::vector<Actor<KeyPublic>> &actors) {
    std::vector<const Actor<KeyPublic> *> batch;
    batch.reserve(actors.size());
    for (const auto &actor : actors) {
        batch.push_back(&actor);
    }
    auto result = save_actor_batch(batch);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    for (const auto &id : *result) {
        emit actorSaved(id);
    }
    return {};
}

std::expected<void, ActorSaveError> ActorIndex::save_actors() {
    std::vector<const Actor<KeyPublic> *> batch;
    batch.reserve(actors_todo_map_.size());
    for (const auto &[id, actor] : actors_todo_map_) {
        batch.push_back(&actor);
    }
    auto result = save_actor_batch(batch);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    actors_todo_map_.clear();
    emit firstSyncProgress(int(result->size()), int(synch_count_));
    return {};
}

std::expected<std::vector<ActorId>, ActorSaveError>
ActorIndex::save_actor_batch(const std::vector<const Actor<KeyPublic> *> &actors) {
    std::vector<ActorId> added;
    if (actors.empty()) {
        return added;
    }
    added.reserve(actors.size());
    std::vector<QString> created;
    created.reserve(actors.size());
    // Hold the actor lock across the transaction, not just each individual query
    std::unique_lock lock(*actorDatabaseLock);
    DbConnector db(folder_path_ + "actors", DbConnectorType::Regular, actorDatabaseLock);
    if (!db.open()) {
        return std::unexpected(ActorSaveError::NotOpened);
    }
    if (!db.query("BEGIN IMMEDIATE")) {
        return std::unexpected(ActorSaveError::Undefined);
    }
    bool committed = false;
    const auto rollback = qScopeGuard([&] {
        if (committed) {
            return;
        }
        if (!sqlite3_get_autocommit(db.getDb()) && !db.query("ROLLBACK")) {
            eCritical("[ActorIndex] Rollback failed; retaining actor files");
            return;
        }
        for (const auto &path : created) {
            if (!QFile::remove(path)) {
                eCritical("[ActorIndex] Failed to remove an uncommitted actor file");
            }
        }
    });
    for (const auto *actor : actors) {
        auto result = add(actor->id(), actor->toJson());
        if (!result.has_value()) {
            if (result.error() == ActorSaveError::AlreadyExists) {
                continue;
            }
            return std::unexpected(result.error());
        }
        created.push_back(QString::fromStdString(build_actor_path(actor->id())));
        if (!db.insert(Config::DataStorage::actorsTable,
                       { { "id", actor->id().to_string() }, { "type", std::to_string(int(actor->type())) } })) {
            return std::unexpected(ActorSaveError::Undefined);
        }
        added.push_back(actor->id());
    }
    if (!db.query("COMMIT")) {
        return std::unexpected(ActorSaveError::Undefined);
    }
    committed = true;
    records_ += added.size();
    synch_.apply_received_ids(added);
    return added;
}

std::vector<ActorId> ActorIndex::read_all_actors_ids() {
    std::vector<ActorId> result;

    DbConnector db(folder_path_ + "actors", DbConnectorType::Regular, actorDatabaseLock);
    db.open();
    auto actors = db.select("SELECT id FROM Actors ORDER by id");
    for (auto &actor : actors) {
        result.push_back(ActorId(actor["id"]));
    }

    return result;
}

bool ActorIndex::is_prepare() {
    return sync_first_done_;
}
