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

#ifndef ACTORINDEX_H
#define ACTORINDEX_H

#include "extrachain_global.h"

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "managers/extrachain_node.h"
#include "network/network_manager.h"

class ExtraChainNode;

/**
 * @brief Actors that stored in blockchain
 */
class EXTRACHAIN_EXPORT ActorIndex {
private:
    ExtraChainNode &node;

    int64_t records = 0;
    const std::string folderPath = DataStorage::BLOCKCHAIN_INDEX.toStdString() + "/"
        + DataStorage::ACTOR_INDEX_FOLDER_NAME.toStdString() + '/';
    int16_t SECTION_NAME_SIZE = 2;
    ActorId m_firstId;

public:
    /**
     * @brief ActorIndex
     */
    explicit ActorIndex(ExtraChainNode &node);
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
    QString buildFilePath(const ActorId &id) const;
    std::string actorPath(const ActorId &id) const;
    /**
     * @brief add
     * @param ActorId id actorId for add
     * @param data
     * @return
     */
    int add(const ActorId &id, const QByteArray &data);
    void sendGetActorMessage(const ActorId &actorId);

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

    /**
     * @brief Validates block digital signature
     * @param block
     * @return true if block is valid
     */
    bool validateBlock(const Block &block);

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

    qint64 getRecords() const;
    void setFirstId(const ActorId &value);
    std::string getFolderPath() const;

    /**
     * @brief Attempts to save actor to local storage
     * @param actor
     */
    void handleNewActor(Actor<KeyPublic> actor);
    /**
     * @brief Serializes an actor and make a file in fs.
     * @param actor
     * @return resultCode, 0 - actor is saved
     */
    int addActor(const Actor<KeyPublic> &actor);
    QByteArrayList allActors();
    std::vector<std::string> allActorsStd();
    void handleNewAllActors(const std::vector<std::string> &actors);

    void handleGetActor(const ActorId &actorId, const std::string &messageId);
    void handleGetAllActor(const ActorId &ignoredActorId, const std::string &messageId);
    void getAllActors(ActorId id, bool isUser);
    void getActorCount(const QByteArray &requestHash, const std::string &messageId);
};

#endif // ACTORINDEX_H
