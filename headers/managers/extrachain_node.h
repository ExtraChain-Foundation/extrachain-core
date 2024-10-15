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

#include <memory>
#include <functional>
#include <expected>

#include <QCoreApplication>
#include <QMap>
#include <QObject>

#include "datastorage/transaction.h"
#include "extrachain_global.h"
#include "utils/vpn_types.h"

class DfsController;
class ActorIndex;
class Blockchain;
class NetworkManager;
class TransactionManager;
class AccountController;
class Transaction;
class ActorId;
class BigNumber;
class DataMiningManager;
template <typename T>
class Actor;
class KeyPrivate;
class KeyPublic;
class ConnectionsManager;
class VPNConnectorManager;
class CreateTokenManager;
struct VPNMessage;
class ExtraChainNode;
// class RestApiServerManager;

class EXTRACHAIN_EXPORT ExtraChainNodeWrapper : public QObject {
    Q_OBJECT

public:
    ExtraChainNodeWrapper(
        QObject* parent,
        bool     isClientApp           = false,
        bool     allowRunRestApiServer = false,
        bool     isRaccoon             = false);

    ~ExtraChainNodeWrapper();

    void Init(bool makeAsync = false);

    ExtraChainNode* node;

private:
    QThread* m_thread = nullptr;
};

class EXTRACHAIN_EXPORT ExtraChainNode : public QObject {
    Q_OBJECT

public:
    typedef std::function<bool(
        VPNMessage&         networkInput,
        ActorId&            senderId,
        VPNFunctionType     funcType,
        VPNFunctionsResult& output)>
                                  VpnFunctionType;
    typedef std::function<void()> VpnFunctionClearType;

private:
    // common object for
    DfsController*      m_dfs                = nullptr;
    ActorIndex*         m_actorIndex         = nullptr;
    Blockchain*         m_blockchain         = nullptr;
    NetworkManager*     m_networkManager     = nullptr;
    TransactionManager* m_transactionManager = nullptr;
    AccountController*  m_accountController  = nullptr;
    DataMiningManager*  m_dmm                = nullptr;
    ConnectionsManager* m_connectionsManager = nullptr;
    std::shared_ptr<CreateTokenManager> m_createTokenManager = nullptr;

    // RestApiServerManager *m_restApiServerManager = nullptr;
    // ContractManager *m_contractManager = nullptr;

    bool                   started             = false;
    bool                   isClientApplication = false;
    bool                   allowRunRestApiServer = false;
    uint64_t               blockCount;
    std::vector<BigNumber> resiveCounts;

    VpnFunctionClearType m_vpnClearFunc = nullptr;

public:
    ~ExtraChainNode();

    bool createNewNetwork(
        const QString& email,
        const QString& password,
        const QString& tokenName,
        const QString& tokenCount,
        const QString& tokenColor);
    void start();

    bool isClientApp() {
        return isClientApplication;
    };

    Blockchain*         blockchain();
    NetworkManager*     network();
    AccountController*  accountController() const;
    ActorIndex*         actorIndex() const;
    DfsController*      dfs() const;
    TransactionManager* transactionManager() const;
    DataMiningManager*  dataMiningManager() const;
    ConnectionsManager* connectionsManager() const;

    bool login(const std::string& login, const std::string& password);
    bool login(const std::string& hash);
    void logout();

    /**
     * @brief Create new transaction from current user
     * @param tx
     */
    std::expected<Transaction, TransactionError> createTransaction(Transaction tx);

    /**
     * @brief Shortcut for another createTransaction method
     * @param receiver - receiver address
     * @param amount - coin count
     */
    std::expected<Transaction, TransactionError>
    createTransaction(ActorId receiver, BigNumberFloat amount, ActorId token);

    std::expected<Transaction, TransactionError>
    createTransactionFrom(ActorId sender, ActorId receiver, BigNumberFloat amount, ActorId token);

    std::string transactionErrorDescription(const TransactionError& error);
    std::string exportUser();
    bool        importUser(const std::string& data, const std::string& login, const std::string& password);
    // TODO: prepareImportUser: get visual info about file

    void createNetworkIdentifier();

    uint64_t getBlockCount() const;

    void                                InitVPN(VpnFunctionType vpnFunc, VpnFunctionClearType vpnClearFun);
    VpnFunctionType                     vpnConnectorManagerFunc = nullptr;
    std::shared_ptr<CreateTokenManager> createTokenManager() const;
    bool                                isRaccoon;

    VPNConfigStorage vpnConfigStorage;

private:
    ExtraChainNode(
        bool     isClientApp           = false,
        bool     allowRunRestApiServer = false,
        bool     isRaccoon             = false);

    friend class ExtraChainNodeWrapper;

    void showMessage(QString from, QString message);
    /**
     * @brief Connect signals between NetworkManager and Blockchain
     */
    void connectTransactionManager();
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
    void InitNode();
    void NodeInitialised();
    void ready();
    void coinResponse(ActorId receiver, BigNumber amount, ActorId plsr);
    void pushNotification(QString actorId, Notification notification);
    void readyInitLocalizationFiles();
    void vpnConnected();
    void vpnDisconnect();

private slots:
    void getAllActorsTimerCall();

public slots:
    void notificationToken(QString os, QString actorId, QString token);
    void handleCountMessageReceived(BigNumber count);

    void calculateBlockCount();
    void cleanUp();
    void InitNodeSlot();
};
#endif // EXTRACHAIN_NODE_H
