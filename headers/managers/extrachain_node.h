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
    DfsController *dfs() const;

    // Remove this function before merge
    void test() const;
    void testPermissions() const;

    bool login(const std::string &login, const std::string &password);
    bool login(const std::string &hash);
    void logout();

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

    std::string exportUser();
    bool importUser(const std::string &data, const std::string &login, const std::string &password);
    // TODO: prepareImportUser: get visual info about file

    void createNetworkIdentifier();

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
    void NewTx(Transaction tx);
    void coinResponse(ActorId receiver, BigNumber amount, ActorId plsr);
    void pushNotification(QString actorId, Notification notification);

private slots:
    void getAllActorsTimerCall();

public slots:
    void notificationToken(QString os, QString actorId, QString token);
};
#endif // EXTRACHAIN_NODE_H
