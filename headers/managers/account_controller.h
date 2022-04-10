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

#ifndef ACCOUNT_CONTROLLER_H
#define ACCOUNT_CONTROLLER_H

#include <QDebug>
#include <QObject>

//#include "enc/enc_tools.h"
#include "datastorage/actor.h"
#include "utils/bignumber.h"
//#include "datastorage/index/actorindex.h"
//#include "enc/key_private.h"

class Blockchain;
class ActorIndex;
class ExtraChainNode;

/**
 * @brief The AccountController class
 * One client can have several accounts, so AccountController is storing this accounts
 * and provides access to them.
 */
class EXTRACHAIN_EXPORT AccountController : public QObject {
    Q_OBJECT

private:
    ExtraChainNode *node;

    std::vector<Actor<KeyPrivate>> m_accounts;
    ActorId m_currentWallet;

public:
    AccountController(ExtraChainNode *node);

public:
    /**
     * @brief Generates a new actor and adds it into accounts list
     * @return created actor
     */
    Actor<KeyPrivate> createUser(ActorType account, QByteArray hashLogin);
    Actor<KeyPrivate> createWallet();
    const Actor<KeyPrivate> &getActor(const ActorId &id);
    const Actor<KeyPrivate> &mainActor();

    /**
     * @brief Gets current active actor
     * @return actor
     */
    const Actor<KeyPrivate> &currentWallet();

    int count() const;

    const std::vector<Actor<KeyPrivate>> &accounts() const;

public slots:
    /**
     * @brief Loads actors from local disk to memory: QList accounts;
     */
    void loadActors(const QByteArray &id, const QByteArrayList &idList, const QByteArray &hashLogin,
                    const std::string &decryptKey);
    /**
     * @brief Saves Private actor on local disk in serialized form
     * @param private actor
     */
    void savePrivateActor(Actor<KeyPrivate> actor, QByteArray hashLogin);
    void clearAcc();
    void changeCurrentWallet(const ActorId &actorId);

signals:
    /**
     * @brief verifyActor
     * @param serialized private actor
     */
    void loadWallets(QByteArray id, QByteArrayList idList);
    void savePrivateProfile(QByteArray id);
    void editPrivateProfile(QByteArray id);
};
#endif // ACCOUNT_CONTROLLER_H
