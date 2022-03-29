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

#ifndef EXTRACHAIN_NODE_H
#define EXTRACHAIN_NODE_H

#include <QCoreApplication>
#include <QMap>
#include <QObject>

#include "datastorage/transaction.h"
#include "extrachain_global.h"

class DfsController;
class ActorIndex;
class Blockchain;
class NetworkManager;
class TransactionManager;
class AccountController;
class SubscribeController;
class PrivateProfile;
class Transaction;
class ActorId;
class BigNumber;
template <typename T>
class Actor;
class KeyPrivate;
class KeyPublic;

class EXTRACHAIN_EXPORT ExtraChainNode : public QObject {
    Q_OBJECT

private:
    // common object for
    DfsController *m_dfs = nullptr;
    ActorIndex *m_actorIndex = nullptr;
    Blockchain *m_blockchain = nullptr;
    NetworkManager *m_networkManager = nullptr;
    TransactionManager *m_txManager = nullptr;
    AccountController *m_accountController = nullptr;
    SubscribeController *m_subscribeController = nullptr;
    PrivateProfile *m_privateProfile = nullptr;
    // ContractManager *m_contractManager = nullptr;

    bool fileMode = true;
    bool started = false;

public:
    ExtraChainNode();
    ~ExtraChainNode();

public:
    bool createNewNetwork(const QString &email, const QString &password, const QString &tokenName,
                          const QString &tokenCount, const QString &tokenColor);
    void start();
    Blockchain *blockchain();
    NetworkManager *network();
    AccountController *accountController() const;
    ActorIndex *actorIndex() const;
    PrivateProfile *privateProfile() const;
    SubscribeController *subscribeController() const;
    DfsController *dfs() const;

    // Remove this function before merge
    void test() const;
    void testPermissions() const;
    void testSerializer() const;

    /**
     * @brief Create new transaction from current user
     * @param tx
     */
    Transaction createTransaction(Transaction tx);

    /**
     * @brief Shortcut for another createTransaction method
     * @param receiver - receiver address
     * @param amount - coin count
     */
    Transaction createTransaction(ActorId receiver, BigNumber amount, ActorId token);

    Transaction createTransactionFrom(ActorId sender, ActorId receiver, BigNumber amount, ActorId token);
    /**
     * @brief createFreezeTransaction
     * if receiver = 0 -> to me
     * @param receiver
     * @param amount
     * @param token
     * @return
     */
    Transaction createFreezeTransaction(ActorId receiver, BigNumber amount, bool toFreeze, ActorId token);

private:
    void showMessage(QString from, QString message);
    /**
     * @brief Connect signals between NetworkManager and Blockchain
     */
    void connectTxManager();
    void connectContractManager();
    void connectBlockchain();
    //    void connectAccountController();
    void connectActorIndex();
    void dfsConnection();
    void connectSignals();
    //    void dfsConnection();
    /**
     * @brief Creates folders for work, if they not exist
     */
    void prepareFolders();

signals:
    void ready();
    void sendMsg(const QByteArray &data, const unsigned int &type);
    void InitNet(ActorIndex *actorChain, AccountController *accountList);
    void NewTx(Transaction tx);
    // created keys for chat
    void sendKey(QByteArray key);
    void sendPrivateKey(QByteArray prKey);
    // public:
    void saveProfile(Actor<KeyPrivate> key, QByteArrayList profile);
    void sendTransactionContract(Transaction tx);
    // void addActorInActorIndex(Actor<KeyPublic> actor);
    void nodeEditPrivateProfile(QPair<QByteArray, QByteArray>, const QString &type, const QByteArray &Data,
                                const bool &reWrite);
    void loadInfoFromPrProfile(const QByteArray &hash, const QByteArray &idProfile, const QString &type);
    void savePrivateProfile(const QByteArray &hash, const ActorId &id);
    void getAllActorsNode(ActorId id, bool acc);
    void login(const QByteArray &login, const QByteArray &password);
    void generateSmartContract(QByteArray tokenCount, QByteArray tokenName, QByteArray rulAddress,
                               QByteArray color);
    void removeConnection(QString identifier);
    void coinResponse(ActorId receiver, BigNumber amount, ActorId plsr);
    void pushNotification(QString actorId, Notification notification);

private slots:
    void getAllActors();
    void getAllActorsTimerCall();
    void logOut();

    // void makeContractFirstTransaction(Contract &contract);
    // void makeContractFinalTransaction(Contract &contract);

public slots:
    void createNetworkIdentifier();
    void notificationToken(QString os, QString actorId, QString token);
};
#endif // EXTRACHAIN_NODE_H
