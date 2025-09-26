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
#include <atomic>

#include <QCoreApplication>
#include <QMap>
#include <QObject>
#include <QTimer>

#include "chain/actor_index.h"
#include "chat/chat.h"
#include "chat/message.h"
#include "dfs/dfs_utils.h"
#include "managers/account_controller.h"
#include "chain/transaction.h"
#include "chain/private_profile.h"
#include "extrachain_global.h"
#include "utils/vpn_types.h"
#include "chain/dag.h"

class DfsController;
class ActorIndex;
class Dag;
class NetworkManager;
class LuminanceManager;
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

enum class ImportProfileFileError {
    LoginPasswordEmpty,
    FileNotFound,
    FileReadError,
    FileEmpty,
    Base64DecodeError,
    ImportError
};

struct SubscriptionRow {
    ActorId     owner_id;
    std::string file_id;

    int           type       = 0;
    std::uint64_t date_start = 0; // block date
    bool          auto_renew = false;
    SectionId     section_id;
    std::string   transaction_hash;
};
BOOST_DESCRIBE_STRUCT(SubscriptionRow, (), (type, date_start, auto_renew, section_id, transaction_hash))

extern std::atomic<bool> node_enabled;

class EXTRACHAIN_EXPORT ExtraChainNodeWrapper : public QObject {
    Q_OBJECT

public:
    ExtraChainNodeWrapper(QObject* parent, bool is_client_application = false, bool is_custom_app = false);

    ~ExtraChainNodeWrapper();

    void Init(bool makeAsync = false);

    ExtraChainNode* node;

private:
    QThread* m_thread = nullptr;
};

class EXTRACHAIN_EXPORT ExtraChainNode : public QObject {
    Q_OBJECT

public:
    typedef std::function<void()> VpnFunctionClearType;

private:
    // common object for
    DfsController*     dfs_                = nullptr;
    ActorIndex*        actor_index_        = nullptr;
    Dag*               dag_                = nullptr;
    LuminanceManager*  luminance_manager_  = nullptr;
    NetworkManager*    network_manager_    = nullptr;
    AccountController* account_controller_ = nullptr;
    DataMiningManager* dmm_                = nullptr;
    TokenManager*      token_manager_      = nullptr;
    ChatManager*       chat_manager_       = nullptr;
    QTimer*            timer_reward_       = nullptr;
    QTimer*            timer_info_         = nullptr;

    bool                        started_               = false;
    bool                        is_client_application_ = false;
    std::vector<BigNumber>      resive_counts_;
    VpnFunctionClearType        vpn_clear_func_ = nullptr;
    std::pair<QString, QString> init_public_ip_and_country_;

    std::optional<SubscriptionRow> subscription_row_;

    std::string                              renames_file_id_waiting_;
    std::unordered_map<ActorId, std::string> renames_todo_;
    std::string                              network_identifier_;

public: // TODO
    std::vector<Actor<KeyPublic>>                 actors_broadcast_;
    std::set<std::pair<std::string, std::string>> identifiers_after_actors_sync_;

public:
    ~ExtraChainNode();

    bool create_new_network(const std::string& login, const std::string& password);
    bool create_new_dag();
    bool create_usernames_vector();
    bool create_chat_templates();
    bool create_subscription_template();
    bool create_token_template();
    bool create_token_vector();
    bool create_renames_template();
    //
    DfsFileStatus create_renames_vector();

    bool write_actor_rename(const ActorId& actor_id, const std::string& name);
    std::vector<std::pair<ActorId, std::string>> read_actor_renames();

    // not only for the one
    bool create_subscription_vector(const std::string& file_name);
    void start();
    bool is_client_application() const;

    std::pair<QString, QString> init_public_ip_and_country() const;

    Dag*               dag() const;
    NetworkManager*    network() const;
    LuminanceManager*  luminance_manager() const;
    AccountController* account_controller() const;
    ActorIndex*        actor_index() const;
    DfsController*     dfs() const;
    DataMiningManager* data_mining_manager() const;

    std::expected<void, LoadError> login(const std::string& login, const std::string& password);
    std::expected<void, LoadError> login(const std::string& hash);
    void                           logout();

    /**
     * @brief Create new transaction from current user
     * @param tx
     */
    std::expected<Transaction, TransactionError> create_transaction(Transaction tx);

    /**
     * @brief Shortcut for another create_transaction method
     * @param receiver - receiver address
     * @param amount - coin count
     */
    std::expected<Transaction, TransactionError> create_transaction(ActorId        receiver,
                                                                    BigNumberFloat amount,
                                                                    ActorId        token);

    std::expected<Transaction, TransactionError> create_transaction_from(ActorId        sender,
                                                                         ActorId        receiver,
                                                                         BigNumberFloat amount,
                                                                         ActorId        token);

    std::expected<Transaction, TransactionError> send_transaction(const Transaction&       transaction,
                                                                  const Actor<KeyPrivate>& signer);

    std::string transaction_error_description(const TransactionError& error);

    std::expected<std::string, ImportError>        export_profile();
    std::expected<std::string, ImportProfileError> import_profile(const std::string& data,
                                                                  const std::string& login,
                                                                  const std::string& password);

    std::expected<std::string, ImportProfileFileError> import_profile_file(const std::string& file_path,
                                                                           const std::string& login,
                                                                           const std::string& password);

    ActorId network_id();
    // TODO: prepareImportUser: get visual info about file

    std::string generate_network_identifier();
    std::string network_identifier();

    void          init_vpn(VpnFunctionClearType vpnClearFun);
    TokenManager* token_manager() const;
    bool          is_custom_app_;

    ChatManager* chat_manager();

    VPNConfigStorage vpnConfigStorage;

    bool add_subscription(const ActorId&     owner_id,
                          const std::string& file_id,
                          int                type,
                          bool               auto_renew,
                          const TokenId&     token_id);

private:
    ExtraChainNode(bool is_client_application = false, bool is_custom_app = false);

    /**
     * @brief Connect signals between NetworkManager and
     */
    //    void connectAccountController();
    void connect_actor_index();
    void connect_dfs();
    void connect_signals();
    //    void dfsConnection();
    /**
     * @brief Creates folders for work, if they not exist
     */
    void prepare_folders();

signals:
    void initNode();
    void finished();
    void nodeInitialised();
    void ready();
    void pushNotification(QString actorId, Notification notification);
    void vpnConnected(std::pair<QString, QString> publicIPAndCountry, bool proxy);
    void vpnDisconnect();

    void subscriptionAdded(ActorId owner_id, std::string file_id);
    void selfTxAdded(const Transaction& tx, StatusTrx::StatusTrxType);
    // void subscriptionRemoved(ActorId owner_id, std::string file_id);

    void dagStatus(DagStatus);
    void dagSyncStart(SectionId, SectionId);
    void dagSyncProgress(SectionId);
    void dagSyncFinish();
    void dagTimerStart(int ms = 15000);
    void dagTimerStop();
    void dagTxSended(SectionId section_id, std::string hash);
    void dagTxApproved(SectionId section_id, std::string hash);
    void dagTxNotApproved(SectionId section_id, std::string hash);

    void dagControlStarted();
    void dagControlEnded();
    void dagControlProgress(SectionId);
    void dagSearchControlStarted();
    void dagSearchControlEnded();

    void chatsLoaded();
    void chatAdded(Chat::Chat chat);
    void messageAdded(ActorId owner_id, std::string file_id, Chat::Message msg);
    void messageRemoved(ActorId owner_id, std::string file_id, std::string id);

    void actorRenamedLoaded();
    void actorRenamed(ActorId actor_id, std::string name);

private slots:
    void timer_reward_request();
    void timer_info_print();

public:
    void selfTxInitContractAdded(const Transaction& transaction);
    void selfTxRepeatableAdded(const Transaction& transaction);

public slots:
    void notificationToken(QString os, QString actorId, QString token);

    void calculateBlockCount();
    void cleanUp();
    void process();

    void dagTimerStarting(int ms);
    void dagTimerStoping();
    void dagTimerTick();

    friend class ExtraChainNodeWrapper;
    friend class NetworkManager;
};
