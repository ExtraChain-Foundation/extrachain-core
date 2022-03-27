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

    // Current user, used in AccountController.
    int userNum = 0;
    QList<Actor<KeyPrivate>> m_accounts;

public:
    AccountController(ExtraChainNode *node);
    QList<QByteArray> getAccountID();

public:
    /**
     * @brief Generates a new actor and adds it into accounts list
     * @return created actor
     */
    Actor<KeyPrivate> createActor(ActorType account, QByteArray hashLogin);
    //    Actor<KeyPrivate> createActorWithId(BigNumber id, bool account, bool contract = false);
    Actor<KeyPrivate> getActor(const ActorId &id);

    /**
     * @brief Gets Actor by public key
     * @param pubkey - serialized public key
     * @return actor
     */
    Actor<KeyPrivate> getActor(int number);
    const Actor<KeyPrivate> &mainActor();

    /**
     * @brief Gets current active actor
     * @return actor
     */
    Actor<KeyPrivate> getCurrentActor();

    int getAccountCount() const;
    int getUserNum() const;
    void setUserNum(int value);

    const QList<Actor<KeyPrivate>> &accounts() const;
    QList<ActorId> getListAccounts() const;

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
    void changeUserNum(QByteArray);
    void process();

signals:
    /**
     * @brief verifyActor
     * @param serialized private actor
     */
    void verifyActor(Actor<KeyPublic> actor);
    void addActorInActorIndex(Actor<KeyPublic> actor);
    //
    void sentActorId(BigNumber actorId);
    void loadWallets(QByteArray id, QByteArrayList idList);
    void updateTransactionListInModel();
    void newActorIsCreated(BigNumber id, bool isUser);
    void savePrivateProfile(QByteArray id);
    void finished();

    void initDfs();
    void editPrivateProfile(QByteArray id);
};
#endif // ACCOUNT_CONTROLLER_H
