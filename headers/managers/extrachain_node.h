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
#include <QCoreApplication>

#include "datastorage/transaction.h"
#include "extrachain_global.h"

#ifdef ECONSOLE
#include "console/console_manager.h"
#endif

class Dfs;
class ActorIndex;
class Blockchain;
class NetworkManager;
class TransactionManager;
class AccountController;
class SmartContractManager;
class ChatManager;
class ResolveManager;
class SubscribeController;
class PrivateProfile;
class Transaction;
class ActorId;
class BigNumber;
template <typename T>
class Actor;
class KeyPrivate;
class KeyPublic;

#ifdef ECONSOLE
#include <QDebug>
#include "datastorage/blockchain.h"
#endif

class EXTRACHAIN_EXPORT ExtraChainNode : public QObject
{
    Q_OBJECT

private:
    // common object for
    bool fileMode = true;
    Dfs *dfs;
    ActorIndex *actorIndex;
    Blockchain *m_blockchain;
    NetworkManager *m_networkManager;
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

public:
    ExtraChainNode(const QString &localIp = "");
    ~ExtraChainNode();

public:
    bool createNewNetwork(const QString &email, const QString &password, const QString &tokenName,
                          const QString &tokenCount, const QString &tokenColor);
    void start();
    Blockchain *blockchain();
    NetworkManager *networkManager();
    AccountController *getAccountController() const;
    ActorIndex *getActorIndex() const;
    ResolveManager *getResolveManager() const;
    PrivateProfile *getPrivateProfile() const;
    SubscribeController *getSubscribeController() const;

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

public:
    void coinResponse(ActorId receiver, BigNumber amount, ActorId plsr);

    QByteArray getIdPrivateProfile() const;
    QByteArray getHashLoginPrivateProfile() const;

    ChatManager *getChatManager() const;

    Dfs *getDfs() const;

private:
    void showMessage(QString from, QString message);
    /**
     * @brief Connect signals between NetworkManager and Blockchain
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
    void savePrivateProfile(const QByteArray &hash, const ActorId &id);
    void setCurrentIdNotificationManager(const QByteArray id);
    void getAllActorsNode(ActorId id, bool acc);
    void loadProfileForConsoleLogin(const QByteArray &login, const QByteArray &password);
    void generateSmartContract(QByteArray tokenCount, QByteArray tokenName, QByteArray rulAddress,
                               QByteArray color);
    void removeConnection(QString identifier);

private slots:
    void getAllActors();
    void getAllActorsTimerCall();
    void logOut();

    // void makeContractFirstTransaction(Contract &contract);
    // void makeContractFinalTransaction(Contract &contract);

public slots:
    void createNetworkIdentifier();
    void setIdPrivateProfile(QByteArray id);          //
    void setHashLoginPrivateProfile(QByteArray hash); //
    void tempareSlotForActors();

    // test net & blockchain
    //    void CheckBlockCount(BigNumber blockCount, QHostAddress peerAddress);
    //    void makeFirstContractTransaction(Contract contract);
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
