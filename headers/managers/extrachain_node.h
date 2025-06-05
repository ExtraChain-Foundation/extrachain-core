/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once

#include <memory>
#include <functional>
#include <expected>

#include <QCoreApplication>
#include <QMap>
#include <QObject>
#include <QTimer>

#include "chain/actor_index.h"
#include "chat/chat.h"
#include "chat/message.h"
#include "managers/account_controller.h"
#include "chain/transaction.h"
#include "chain/private_profile.h"
#include "extrachain_global.h"
#include "utils/vpn_types.h"
#include "chain/dag.h"

#include <atomic>

static std::atomic<bool> node_enabled { true };

class DfsController;
class ActorIndex;
class Dag;
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
class TokenManager;
class SubscriptionManager;
struct VPNMessage;
class ExtraChainNode;
enum class MessageType;
enum class MessageStatus;
class WebSocketService;
class ChatManager;

enum class ImportProfileError {
    DataEmpty,
    LoginPasswordEmpty,
    DecryptError,
    IncorrectJson
};

class EXTRACHAIN_EXPORT ExtraChainNodeWrapper : public QObject {
    Q_OBJECT

public:
    ExtraChainNodeWrapper(QObject* parent, bool isRaccoon = false);
    ~ExtraChainNodeWrapper();

    void            Init(bool makeAsync = false);
    ExtraChainNode* node;

private:
    QThread* m_thread = nullptr;
};

//
class EXTRACHAIN_EXPORT ExtraChainNode : public QObject {
    Q_OBJECT

public:
    typedef std::function<void()> VpnFunctionClearType;

private:
    // common object for
    DfsController*       dfs_                  = nullptr;
    ActorIndex*          actor_index_          = nullptr;
    Dag*                 dag_                  = nullptr;
    NetworkManager*      network_manager_      = nullptr;
    AccountController*   account_controller_   = nullptr;
    DataMiningManager*   mining_manager_       = nullptr;
    TokenManager*        token_manager_        = nullptr;
    SubscriptionManager* subscription_manager_ = nullptr;
    ChatManager*         chat_manager_         = nullptr;
    QTimer*              timer_all_actors_     = nullptr;
    QTimer*              timer_reward_         = nullptr;
    QTimer*              timer_info_           = nullptr;

    bool                        started_       = false;
    VpnFunctionClearType        m_vpnClearFunc = nullptr;
    std::pair<QString, QString> m_initPublicIPAndCountry;

public:
    std::vector<Actor<KeyPublic>> actors_broadcast_;

public:
    ~ExtraChainNode();

    bool create_new_network(const std::string& login, const std::string& password);
    bool create_new_dag();
    bool create_usernames_vector();
    bool create_chat_templates();
    bool create_subscription_template();
    bool create_token_template();
    bool create_token_vector();

    void start();

    std::pair<QString, QString> getInitPublicIPAndCountry() const;

    Dag*               dag();
    NetworkManager*    network();
    AccountController* accountController() const;
    ActorIndex*        actorIndex() const;
    DfsController*     dfs() const;
    DataMiningManager* mining_manager() const;

    std::expected<void, LoadError> login(const std::string& login, const std::string& password);
    std::expected<void, LoadError> login(const std::string& hash);
    void                           logout();

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
    std::expected<Transaction, TransactionError> createTransaction(ActorId        receiver,
                                                                   BigNumberFloat amount,
                                                                   ActorId        token);

    std::expected<Transaction, TransactionError> createTransactionFrom(ActorId        sender,
                                                                       ActorId        receiver,
                                                                       BigNumberFloat amount,
                                                                       ActorId        token);

    std::expected<Transaction, TransactionError> send_transaction(const Transaction&       transaction,
                                                                  const Actor<KeyPrivate>& signer);

    std::string transactionErrorDescription(const TransactionError& error);

    std::expected<std::string, ImportError>        export_profile();
    std::expected<std::string, ImportProfileError> import_profile(const std::string& data,
                                                                  const std::string& login,
                                                                  const std::string& password);

    ActorId network_id();
    // TODO: prepareImportUser: get visual info about file

    std::string generate_network_identifier();
    std::string network_identifier();

    void                 InitVPN(VpnFunctionClearType vpnClearFun);
    TokenManager*        token_manager() const;
    SubscriptionManager* subscription_manager() const;
    bool                 isRaccoon;

    ChatManager* chat_manager();

    VPNConfigStorage vpnConfigStorage;

private:
    ExtraChainNode(bool isRaccoon = false);

    friend class ExtraChainNodeWrapper;
    friend class NetworkManager;

    /**
     * @brief Connect signals between NetworkManager and
     */
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
    void finished();
    void NodeInitialised();
    void ready();
    void pushNotification(QString actorId, Notification notification);
    void readyInitLocalizationFiles();
    void vpnConnected(std::pair<QString, QString> publicIPAndCountry, bool proxy);
    void vpnDisconnect();

    void subscriptionAdded(ActorId owner_id, std::string file_id);
    void selfTxAdded(const Transaction& tx);
    // void subscriptionRemoved(ActorId owner_id, std::string file_id);

    void dagStatus(DagStatus);
    void dagSyncStart(BigNumber, BigNumber);
    void dagSyncProgress(BigNumber);

    void chatsLoaded();
    void chatAdded(Chat::Chat chat);
    void messageAdded(ActorId owner_id, std::string file_id, Chat::Message msg);
    void messageRemoved(ActorId owner_id, std::string file_id, std::string id);

private slots:
    void getAllActorsTimerCall();
    void timer_reward_request();
    void timer_info_print();

public slots:
    void notificationToken(QString os, QString actorId, QString token);

    void calculateBlockCount();
    void cleanUp();
    void process();
};
