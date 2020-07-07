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

#ifndef NODE_MANAGER_H
#define NODE_MANAGER_H
#ifndef RESOLVE_MANAGER_DEF
#define RESOLVE_MANAGER_DEF
class ResolveManager;
#include "resolve/resolve_manager.h"
#endif
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
#include "headers/network/packages/service/message_types.h"
#include "managers/file_updater_manager.h"

#include "asyncfuture.h"
#include <QtConcurrent>

#ifdef ECLIENT
#include "ui/ui_controller.h"
#include "headers/ui/notificationclient.h"
#include "managers/notification_manager.h"
#endif

#ifdef ECONSOLE
#include "managers/console_manager.h"
#endif

using namespace DfsStruct;
class NodeManager : public QObject
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
    UiController *uiController;
    WalletController *uiWallet;
    NotificationClient *notificationClient = nullptr;
    NotificationManager *notifyM;
#endif

public:
    NodeManager();
    ~NodeManager();

public:
    void createCompanyActor(const QString &email, const QString &password);
    Blockchain *getBlockchain();
    NetManager *getNetManager();
    AccountController *getAccController() const;

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
    Transaction createTransaction(BigNumber receiver, BigNumber amount, BigNumber token = 0);

    Transaction createTransactionFrom(BigNumber sender, BigNumber receiver, BigNumber amount,
                                      BigNumber token = 0);
    /**
     * @brief createFreezeTransaction
     * if receiver = 0 -> to me
     * @param receiver
     * @param amount
     * @param token
     * @return
     */
    Transaction createFreezeTransaction(BigNumber receiver, BigNumber amount, bool toFreeze,
                                        BigNumber token = 0);

    int getClientList();

public:
    void coinResponse(BigNumber receiver, BigNumber amount, BigNumber plsr);

#ifdef ECLIENT
    UiController *getUiController() const;
    void setNotificationClient(NotificationClient *newNtfCl);
#endif

    QByteArray getIdPrivateProfile() const;
    QByteArray getHashLoginPrivateProfile() const;

    ChatManager *getChatManager() const;

    Dfs *getDfs() const;

private:
    Actor<KeyPrivate> CreateCompany(QByteArray consoleHash);
    void showMessage(QString from, QString message);
    /**
     * @brief Connect signals between NetManager and Blockchain
     */
    void connectResolveManager();
    void connectSmContractManager();
    void connectTxManager();
    void connectUi();
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
    void profileToUi(QString actorId, Profile profile);
    void sendTransactionContract(Transaction tx);
    //    void addActorInActorIndex(Actor<KeyPublic> actor);
    void nodeEditPrivateProfile(QPair<QByteArray, QByteArray>, const QString &type, const QByteArray &Data,
                                const bool &reWrite);
    void loadInfoFromPrProfile(const QByteArray &hash, const QByteArray &idProfile, const QString &type);
    void savePrivateProfile(const QByteArray &hash, const QByteArray &id);
    void setCurrentIdNotifyM(const QByteArray id);
    void getAllActorsNode(QByteArray id, bool acc);
    void loadProfileForConsoleLogin(const QByteArray &login, const QByteArray &password);
    void generateSmartContract(QByteArray tokenCount, QByteArray tokenName, QByteArray rulAddress,
                               QByteArray color);

private slots:
    void initConsoleToken(Transaction tx);
    void getAllActors();
    void getAllActorsTimerCall();
    void setIdPrivateProfile(QByteArray id);
    void setHashLoginPrivateProfile(QByteArray hash);
    void logOut();

    //    void makeContractFirstTransaction(Contract &contract);
    //    void makeContractFinalTransaction(Contract &contract);
public slots:
    void tempareSlotForActors();

    // test net & blockchain

    //    void CheckBlockCount(BigNumber blockCount, QHostAddress peerAddress);
    //    void makeFirstContractTransaction(Contract contract);
    void createNetManagerIdentificator();
    void dfscreateNetManagerIdentificator();
#ifdef ECLIENT
    void sendTransactionFromUi(BigNumber reciever, BigNumber actor, BigNumber token);

private slots:
    void createWalletInUi();
    void updateWalletInUi();
    void updateWalletList();
    void updateAvailableWalletList();
    void updateRecentActivities();
    void changeWalletIdUi(BigNumber walletId);
    void addNewWallet();
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

    void sendCoinRequest(BigNumber receiver, BigNumber amount)
    {
        qInfo().noquote() << "Sending" << Transaction::amountToVisible(amount) << "coins to"
                          << receiver.toByteArray();
        createTransaction(receiver, amount, 0);
    }

private:
    ConsoleManager *m_consoleManager;
    QList<std::tuple<BigNumber, BigNumber, BigNumber>> m_requestCoinQueue;
    bool m_listenCoinRequest = false;
#endif
};
#endif // NODE_MANAGER_H
