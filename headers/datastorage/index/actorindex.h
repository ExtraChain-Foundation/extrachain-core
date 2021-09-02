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

#include <QHostAddress>

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include <datastorage/searchfilters.h>
#include "profile/public_profile.h"
#include "network/socket_pair.h"
#include "network/packages/base_message_response.h"
#include "network/packages/service/all_messages.h"
#include "network/packages/service/message_types.h"

class ResolveManager;
class AccountController;

/**
 * @brief Actors that stored in blockchain
 */
class ActorIndex : public QObject
{
    Q_OBJECT

private:
    AccountController *accController = nullptr;
    ResolveManager *resolveManager = nullptr;
    qint64 records = 0;
    const QString folderPath =
        DataStorage::BLOCKCHAIN_INDEX + "/" + DataStorage::ACTOR_INDEX_FOLDER_NAME + '/';
    short SECTION_NAME_SIZE = 2;
    QMap<QByteArray, QByteArray> profilesHandle;
    ActorId m_firstId;

public:
    ActorId firstId();

    /**
     * @brief ActorIndex
     */
    ActorIndex(QObject *parent = nullptr);
    /**
     * @brief ~ActorIndex
     */
    ~ActorIndex();

private:
    /**
     * @brief buildFilePath
     * @param id
     * @return
     */
    QString buildFilePath(const QByteArray &id) const;
    QString buildPathPubProfile(const QByteArray &id);
    /**
     * @brief add
     * @param BigNumber id actorId for add
     * @param data
     * @return
     */
    int add(const ActorId &id, const QByteArray &data);

public:
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
    bool hasActor(const ActorId &id);
    void removeActor(const ActorId &id, bool resend = false);

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
    QString getFolderPath() const;

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
    void handleNewAllActors(const QByteArrayList actors);

public:
    void setResolveManager(ResolveManager *value);

    void setAccController(AccountController *value);

public slots:
    void process();
    void handleGetActor(const ActorId &actorId, QByteArray reqHash, const SocketPair &receiver);
    void handleGetAllActor(QByteArray reqHash, const SocketPair &receiver);
    void getAllActors(ActorId id, bool isUser);
    void getActorCount(const QByteArray &requestHash, const SocketPair &receiver);

    void saveProfile(Actor<KeyPrivate> *actor, QByteArrayList newProfile);
    void saveProfileFromNetwork(const QByteArray &newProfile);
    void requestProfile(QString id);
    QByteArrayList getProfile(QString id);

    /**
     * @brief
     */
    void removeAll();

signals:
    /**
     * @brief sendMessage to NetworkManager slot: sendMessage
     * @param data
     * @param type
     */
    void sendMessage(const QByteArray &data, const unsigned int &type);
    /**
     * @brief responseReady
     * @param data
     * @param msgType
     * @param requestHash
     * @param receiver
     */
    void responseReady(const QByteArray &data, const unsigned int &msgType, const QByteArray &requestHash,
                       const SocketPair &receiver);

    void profileAvailabled(QString userId, QByteArrayList profile);
    // void PrivateActorIsVerified(Actor<KeyPrivate> actor);
    void PublicActorIsVerified(Actor<KeyPublic> actor); // unused

    void initDfs(ActorId userId);
    void initContractList(QVariantMap map);
    void finished();
};

#endif // ACTORINDEX_H
