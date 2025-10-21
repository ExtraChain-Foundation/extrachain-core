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

#pragma once

#include "extrachain_global.h"

#include "chain/actor.h"
#include "managers/extrachain_node.h"
#include "chain/actor_filter.h"

class ExtraChainNode;
class Responder;

enum class ActorIndexError {
    Unknown,
    NoActor,
    ZeroActor
};

enum class ActorSaveError {
    Undefined,
    NotExists,
    AlreadyExists,
    NotOpened
};

enum class ActorGetType {
    NoRequest,
    Request
};

/**
 * @brief Actors that stored in chain
 */
class EXTRACHAIN_EXPORT ActorIndex : public QObject {
    Q_OBJECT

private:
    ExtraChainNode *node;

    const std::string folder_path_      = fmt::format("{}/", ChainConst::ACTORS_FOLDER);
    const int16_t     SECTION_NAME_SIZE = 2;

    std::uint64_t records_ = 0;
    ActorId       network_id_;

    ActorSynchronizer                       synch_;
    std::uint64_t                           synch_count_     = 0;
    bool                                    sync_first_done_ = false;
    std::map<std::string, Actor<KeyPublic>> actors_todo_map_;

public:
    /**
     * @brief ActorIndex
     */
    explicit ActorIndex(ExtraChainNode *node);
    /**
     * @brief ~ActorIndex
     */
    ~ActorIndex() = default;

private:
    /**
     * @brief build_file_path
     * @param id
     * @return
     */
    QString     build_file_path(const ActorId &id) const;
    std::string build_actor_path(const ActorId &id) const;

    /**
     * @brief add
     * @param ActorId id actorId for add
     * @param data
     * @return
     */
    std::expected<void, ActorSaveError> add(const ActorId &id, const QByteArray &data);
    void                                send_get_actor_message(const ActorId &actorId);
    bool                                save_actor_index(const Actor<KeyPublic> &actor);

public:
    ActorId network_id();

    /**
     * @brief Check actor with actorId exist
     * @param actorId
     * @return resultCode, true - exist, false - none
     */
    bool exists(const ActorId &actor_id);

    /**
     * @brief Read actor from local storage
     * @param id - actor's id
     * @return Found actor, or empty actor (if not found)
     */
    Actor<KeyPublic> read_actor_old(const ActorId &id);

    std::expected<Actor<KeyPublic>, ActorIndexError> read_actor(const ActorId &id,
                                                                ActorGetType   get_type = ActorGetType::Request);

    /**
     * @brief read_by_id
     * @param id
     * @return
     */
    QByteArray read_by_id(const ActorId &id) const;

    std::size_t records() const;
    std::string folder_path() const;
    void        set_network_id(const ActorId &value);

    /**
     * @brief Serializes an actor and make a file in fs.
     * @param actor
     * @return resultCode, 0 - actor is saved
     */
    std::expected<void, ActorSaveError> store_new_actor(const Actor<KeyPublic> &actor);

    std::expected<void, ActorSaveError> network_store_new_actor(const Actor<KeyPublic> &actor);
    std::expected<void, ActorSaveError> save_actor(const Actor<KeyPublic> &actor);
    std::expected<void, ActorSaveError> save_actors();
    std::vector<ActorId>                read_all_actors_ids();
    bool                                is_prepare();

    void network_actors_request(const std::set<ActorId> &actors, const Responder &responder);
    void network_actors_response(const std::vector<Actor<KeyPublic>> &actors);

    void send_system_actor(const Responder &responder);

    void network_actor_request(const ActorId &actorId, const Responder &responder);

    void request_actors_hash(const Responder &responder);
    void network_actors_hash_request(std::uint64_t               count,
                                     const std::vector<uint8_t> &bits,
                                     const Responder            &responder);

signals:
    void newActorSaved(ActorId actor_id);
    void actorSaved(ActorId actor_id);
    void firstSyncStarted();
    void firstSyncEnded();
    void firstSyncProgress(int progress, int all);
};
