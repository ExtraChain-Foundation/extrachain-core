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

#include <QObject>
#include <QMap>
#include "network/network_manager.h"
#include "managers/tx_manager.h"
#include "managers/account_controller.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/blockchain.h"
#include "datastorage/block.h"
#include "datastorage/transaction.h"
#include "datastorage/actor.h"
#include "managers/thread_pool.h"
#include "dfs/controls/headers/dfs.h"
#include "managers/contract_manager.h"
#include "managers/sm_manager.h"
#include "dfs/managers/headers/dfsnetmanager.h"
#include "managers/chatmanager.h"
#include "profile/private_profile.h"
#include "dfs/controls/headers/subscribe_controller.h"
#include "network/packages/service/message_types.h"
#include "managers/file_updater_manager.h"

#include <QtConcurrent>
#include <QCoreApplication>

#ifdef ECLIENT
#include "ui/notificationclient.h"
#include "managers/notification_manager.h"
#endif

#ifdef ECONSOLE
#include "managers/console_manager.h"
#endif

class ResolveManager;

class ExtraChainNode : public QObject
{
    Q_OBJECT

private:
    // common object for
    bool fileMode = true;
    Dfs *dfs;
    ActorIndex *actorIndex;
    Blockchain *blockchain;
    NetManager *netManager;
    TransactionManager *txManager;
    AccountController *accController;
    SmartContractManager *smContractController;
    ChatManager *chatManager;
    ResolveManager *resolveManager;
    SubscribeController *subscribeController;
    PrivateProfile *prProfile;
    // ContractManager *contractManager;

    QByteArray idPrivateProfile;
    QByteArray hashLoginPrivateProfile;

#ifdef ECLIENT
    NotificationClient *notificationClient = nullptr; // TODO: move to client
public:
    NotificationManager *notificationManager; // TODO: make for core
#endif

public:
    ExtraChainNode(const QString &localIp = "");
    ~ExtraChainNode();

public:
    void createNewNetwork(const QString &email, const QString &password);
    void start();
    Blockchain *getBlockchain();
    NetManager *getNetManager();
    AccountController *getAccountController() const;
    ActorIndex *getActorIndex() const;
    ResolveManager *getResolveManager() const;
    PrivateProfile *getPrivateProfile() const;
    SubscribeController *getSubscribeController() const;
#ifdef ECLIENT
    NotificationManager *getNotificationManager() const;
#endif

    void getBlockchainFile();

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
    Transaction createTransaction(ActorId receiver, BigNumber amount, ActorId token = ActorId());

    Transaction createTransactionFrom(ActorId sender, ActorId receiver, BigNumber amount,
                                      ActorId token = ActorId());
    /**
     * @brief createFreezeTransaction
     * if receiver = 0 -> to me
     * @param receiver
     * @param amount
     * @param token
     * @return
     */
    Transaction createFreezeTransaction(ActorId receiver, BigNumber amount, bool toFreeze,
                                        ActorId token = ActorId());

    int getClientList();

public:
    void coinResponse(ActorId receiver, BigNumber amount, ActorId plsr);

#ifdef ECLIENT
    void setNotificationClient(NotificationClient *newNtfCl);
#endif

    QByteArray getIdPrivateProfile() const;
    QByteArray getHashLoginPrivateProfile() const;

    ChatManager *getChatManager() const;

    Dfs *getDfs() const;

private:
    void showMessage(QString from, QString message);
    /**
     * @brief Connect signals between NetManager and Blockchain
     */
    void connectResolveManager();
    void connectSmContractManager();
    void connectTxManager();
    void connectConsole();
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
    void sendActorToWallet(QList<QByteArray> list);
    void sendActorStateList(QMap<QByteArray, QByteArray> map);
    void saveProfile(Actor<KeyPrivate> *key, QByteArrayList profile);
    void sendTransactionContract(Transaction tx);
    //    void addActorInActorIndex(Actor<KeyPublic> actor);
    void nodeEditPrivateProfile(QPair<QByteArray, QByteArray>, const QString &type, const QByteArray &Data,
                                const bool &reWrite);
    void loadInfoFromPrProfile(const QByteArray &hash, const QByteArray &idProfile, const QString &type);
    void savePrivateProfile(const QByteArray &hash, const QByteArray &id);
    void setCurrentIdNotificationManager(const QByteArray id);
    void getAllActorsNode(ActorId id, bool acc);
    void loadProfileForConsoleLogin(const QByteArray &login, const QByteArray &password);
    void generateSmartContract(QByteArray tokenCount, QByteArray tokenName, QByteArray rulAddress,
                               QByteArray color);

private slots:
    void initConsoleToken(Transaction tx);
    void getAllActors();
    void getAllActorsTimerCall();
    void logOut();

    //    void makeContractFirstTransaction(Contract &contract);
    //    void makeContractFinalTransaction(Contract &contract);
public slots:
    void setIdPrivateProfile(QByteArray id);          //
    void setHashLoginPrivateProfile(QByteArray hash); //
    void tempareSlotForActors();

    // test net & blockchain
    //    void CheckBlockCount(BigNumber blockCount, QHostAddress peerAddress);
    //    void makeFirstContractTransaction(Contract contract);
    void createNetManagerIdentificator();
    void dfscreateNetManagerIdentificator();
#ifdef ECLIENT
    void notificationToken(QString os, QString actorId, QString token);
#endif

#ifdef ECONSOLE
signals:
    void pushNotification(QString actorId, Notification notification);

public: // TODO
    ConsoleManager *consoleManager()
    {
        return m_consoleManager;
    }

    void setConsoleManager(ConsoleManager *consoleManager)
    {
        this->m_consoleManager = consoleManager;
    }

    auto &requestCoinQueue()
    {
        return m_requestCoinQueue;
    }

    void setListenCoinRequest(bool listenCoinRequest)
    {
        m_listenCoinRequest = listenCoinRequest;
    }

    bool listenCoinRequest()
    {
        return m_listenCoinRequest;
    }

    void sendCoinRequest(ActorId receiver, BigNumber amount)
    {
        qInfo().noquote() << "Sending" << Transaction::amountToVisible(amount) << "coins to"
                          << receiver.toByteArray();
        createTransaction(receiver, amount, ActorId());
    }

private:
    ConsoleManager *m_consoleManager;
    QList<std::tuple<ActorId, BigNumber, ActorId>> m_requestCoinQueue;
    bool m_listenCoinRequest = false;
#endif
};
#endif // EXTRACHAIN_NODE_H
