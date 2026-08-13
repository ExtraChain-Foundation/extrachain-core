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

#include <thread>

#include "network/network_service.h"
#include "utils/file_io.h"
#include "utils/thread_pool_boost.h"

ActorId ActorIndex::network_id() {
    return network_id_;
}

ActorIndex::ActorIndex(ExtraChain::Core::ExtraChainNode *node)
    : node(node) {
    DbConnector db(folder_path_ + "actors");
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

ExtraChain::Core::Event<ActorId> &ActorIndex::new_actor_saved_event() {
    return new_actor_saved_event_;
}

ExtraChain::Core::Event<ActorId> &ActorIndex::actor_saved_event() {
    return actor_saved_event_;
}

ExtraChain::Core::Event<> &ActorIndex::first_sync_started_event() {
    return first_sync_started_event_;
}

ExtraChain::Core::Event<> &ActorIndex::first_sync_ended_event() {
    return first_sync_ended_event_;
}

ExtraChain::Core::Event<int, int> &ActorIndex::first_sync_progress_event() {
    return first_sync_progress_event_;
}

Actor<KeyPublic> ActorIndex::read_actor_old(const ActorId &id) {
    if (id.is_zero()) {
        eWarning("[ActorIndex] Error: try get actor with id: {}", id);
        return Actor<KeyPublic>();
    }

    const auto serialized_actor = this->read_by_id(id);
    if (!serialized_actor.empty()) {
        auto actor = Actor<KeyPublic>::fromJson(serialized_actor);
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

    std::string serialized_actor = this->read_by_id(id);
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
            auto save_result = save_actors();
            if (!save_result.has_value()) {
                eWarning("[ActorIndex] Cannot finish actor sync: error {}", static_cast<int>(save_result.error()));
                return;
            }

            sync_first_done_ = true;
            node->account_controller()->dogenerate();
            first_sync_ended_event_.publish();
        }
    } else {
        for (const auto &actor : actors) {
            auto save_result = save_actor(actor);
            if (!save_result.has_value() && save_result.error() != ActorSaveError::AlreadyExists) {
                eWarning("[ActorIndex] Cannot save actor {}: error {}",
                         actor.id(),
                         static_cast<int>(save_result.error()));
            }

            if (!node_enabled.load()) {
                return;
            }
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
        first_sync_started_event_.publish();
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

    if (records_ + 1 >= count) {
        if (!sync_first_done_) {
            first_sync_ended_event_.publish();
        }

        sync_first_done_ = true;
        node->account_controller()->dogenerate();
    }

    auto r = responder;
    ThreadPoolBoost::instance()->post([this, responder = r, actor_ids] {
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
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
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

std::filesystem::path ActorIndex::build_file_path(const ActorId &id) const {
    const auto &actor_id = id.to_string();
    const auto folder = std::filesystem::path(folder_path_) / actor_id.substr(actor_id.size() - SECTION_NAME_SIZE);
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    return folder / actor_id;
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

std::expected<void, ActorSaveError> ActorIndex::add(const ActorId &id, std::string_view data) {
    const auto      path = build_file_path(id);
    std::error_code error;
    if (std::filesystem::exists(path, error)) {
        // eLog("[ActorIndex] Can't save file {}: already exist", path);
        return std::unexpected(ActorSaveError::AlreadyExists);
    }

    if (!FileIo::write_atomic(path, data).has_value()) {
        eLog("[ActorIndex] Can't save file {}: not opened", path);
        return std::unexpected(ActorSaveError::NotOpened);
    }
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

std::string ActorIndex::read_by_id(const ActorId &id) const {
    return FileIo::read_all(build_actor_path(id)).value_or(std::string {});
}

std::expected<void, ActorSaveError> ActorIndex::store_new_actor(const Actor<KeyPublic> &actor) {
    auto result = this->save_actor(actor);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }

    new_actor_saved_event_.publish(actor.id());

    if (node->network()->is_active_connection_exists()) {
        node->network()->send_broadcast(actor, MessageType::NewActor);
    } else {
        node->queue_actor_broadcast(actor);
    }

    return result;
}

std::expected<void, ActorSaveError> ActorIndex::network_store_new_actor(const Actor<KeyPublic> &actor) {
    auto result = this->save_actor(actor);
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }

    new_actor_saved_event_.publish(actor.id());
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

    synch_.apply_received_ids({ actor.id() });
    actor_saved_event_.publish(actor.id());
    return {};
}

std::expected<void, ActorSaveError> ActorIndex::save_actors() {
    DbConnector db(folder_path_ + "actors");
    if (!db.open()) {
        return std::unexpected(ActorSaveError::NotOpened);
    }

    db.query("BEGIN TRANSACTION");

    int i          = 0;
    int to_records = 0;
    for (const auto &[id, actor] : actors_todo_map_) {
        auto result = this->add(actor.id(), actor.toJson());
        if (!result.has_value()) {
            // eWarning("[ActorIndex] Saving actor {} error: {}", actor.id(), result.error());
            continue;
        }

        ++i;
        if (i % 100 == 0) {
            first_sync_progress_event_.publish(i, static_cast<int>(synch_count_));
        }

        bool dbInsert =
            db.insert(Config::DataStorage::actorsTable,
                      { { "id", actor.id().to_string() }, { "type", std::to_string(int(actor.type())) } });

        synch_.apply_received_ids({ actor.id() }); // TODO
        to_records++;
    }

    // if (!dbInsert) {
    //     eCritical("db actor insert error");
    //     return false;
    // }
    db.query("COMMIT");
    // eLog("Actors timer: {} ms", timer.elapsed());

    records_ += to_records;
    actors_todo_map_.clear();

    return {};
}

bool ActorIndex::save_actor_index(const Actor<KeyPublic> &actor) {
    this->records_++;

    DbConnector db(folder_path_ + "actors");
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

std::vector<ActorId> ActorIndex::read_all_actors_ids() {
    std::vector<ActorId> result;

    DbConnector db(folder_path_ + "actors");
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
