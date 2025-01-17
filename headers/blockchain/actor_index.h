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

#include "blockchain/actor.h"
#include "blockchain/block_variant.h"
#include "managers/extrachain_node.h"

class ExtraChainNode;

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

/**
 * @brief Actors that stored in blockchain
 */
class EXTRACHAIN_EXPORT ActorIndex : public QObject {
    Q_OBJECT

private:
    ExtraChainNode *node;

    std::uint64_t     records = 0;
    const std::string folderPath =
        fmt::format("{}/{}/", BlockchainConst::BLOCKCHAIN_INDEX, BlockchainConst::ACTOR_INDEX_FOLDER_NAME);
    int16_t SECTION_NAME_SIZE = 2;
    ActorId m_firstId;

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
     * @brief buildFilePath
     * @param id
     * @return
     */
    QString     buildFilePath(const ActorId &id) const;
    std::string actorPath(const ActorId &id) const;
    /**
     * @brief add
     * @param ActorId id actorId for add
     * @param data
     * @return
     */
    std::expected<void, ActorSaveError> add(const ActorId &id, const QByteArray &data);
    void                                sendGetActorMessage(const ActorId &actorId);
    bool                                save_actor_index(const Actor<KeyPublic> &actor);

public:
    ActorId firstId();

    /**
     * @brief Check actor with actorId exist
     * @param actorId
     * @return resultCode, true - exist, false - none
     */
    bool actorExist(const ActorId &actorId);

    /**
     * @brief Gets actor from local storage
     * @param id - actor's id
     * @return Found actor, or empty actor (if not found)
     */
    Actor<KeyPublic> getActor(const ActorId &id);

    std::expected<Actor<KeyPublic>, ActorIndexError> get_actor(const ActorId &id);

    /**
     * @brief Validates block digital signature
     * @param block
     * @return true if block is valid
     */
    bool validateBlock(const BlockVariant &block);

    /**
     * @brief Validates transaction digital signature
     * @param tx
     * @return true if transaction is valid
     */
    bool validateTx(const Transaction &tx);

    /**
     * @brief getById
     * @param id
     * @return
     */
    QByteArray getById(const ActorId &id) const;

    std::size_t getRecords() const;
    void        setFirstId(const ActorId &value);
    std::string getFolderPath() const;

    /**
     * @brief Serializes an actor and make a file in fs.
     * @param actor
     * @return resultCode, 0 - actor is saved
     */
    std::expected<void, ActorSaveError> store_new_actor(const Actor<KeyPublic> &actor);
    std::expected<void, ActorSaveError> save_actor(const Actor<KeyPublic> &actor);
    std::vector<ActorId>                allActors();
    void                                handleNewAllActors(const std::vector<ActorId> &actors);

    void handleGetActor(const ActorId &actorId, const std::string &messageId);
    void handleGetAllActor(const ActorId &ignoredActorId, const std::string &messageId);
    void getAllActors(ActorId id, bool isUser);
    void getActorCount(const QByteArray &requestHash, const std::string &messageId);

signals:
    void newActorSaved(ActorId actor_id);
};
