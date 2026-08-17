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

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/strand.hpp>
#include <boost/signals2/connection.hpp>

#include "chain/actor_index.h"
#include "chat/chat.h"
#include "chat/message.h"
#include "dfs/dfs_utils.h"
#include "managers/account_controller.h"
#include "chain/transaction.h"
#include "chain/private_profile.h"
#include "extrachain_global.h"
#include "chain/dag.h"
#include "contracts/contract_types.h"
#include "contracts/contract_transaction.h"
#include "runtime/event.h"

class DfsService;
class ActorIndex;
class Dag;
class NetworkService;
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
class TokenManager;
enum class MessageType;
enum class MessageStatus;
class WebSocketService;
class ChatManager;
class ThothManager;
class JanusManager;
namespace ExtraChain::Contracts {
    class ContractManager;
    class ToolchainRegistry;
} // namespace ExtraChain::Contracts
namespace ExtraChain::Consensus {
    class ConsensusService;
}
namespace ExtraChain::Core {
    class DeadlineTask;
    class NetworkRuntime;
    class PeriodicTask;
} // namespace ExtraChain::Core

enum class RuntimeProfile {
    MobileLight,
    DesktopLight,
    FullNode
};

enum class RuntimeActivity {
    Foreground,
    Background
};

struct RuntimeLimits {
    std::size_t io_workers;
    std::size_t storage_workers;
    std::size_t compute_workers;
    std::size_t peer_limit;
    std::size_t dfs_downloads;
    std::size_t pack_sync_window;
    std::size_t cached_transactions;
    std::size_t sync_transactions;
    std::size_t derived_sections;
    std::size_t admission_prevalidation_workers;
    std::size_t wasm_concurrency;
    std::size_t wasm_cache_bytes_per_thread;
};

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

namespace ExtraChain::Core {

    class EXTRACHAIN_EXPORT ExtraChainNode {

        using SerialExecutor = boost::asio::strand<boost::asio::any_io_executor>;

    private:
        // common object for
        std::unique_ptr<DfsService>                                                    dfs_;
        std::unique_ptr<ActorIndex>                               actor_index_;
        std::unique_ptr<Dag>                                                           dag_;
        std::unique_ptr<LuminanceManager>                                              luminance_manager_;
        std::unique_ptr<NetworkService>                                                network_service_;
        std::unique_ptr<AccountController>                                             account_controller_;
        std::unique_ptr<DataMiningManager>                                             dmm_;
        std::unique_ptr<TokenManager>                                                  token_manager_;
        std::unique_ptr<ChatManager>                                                   chat_manager_;
        std::unique_ptr<ThothManager>                                                  thoth_manager_;
        std::unique_ptr<JanusManager>                                                  janus_manager_;
        std::unique_ptr<ExtraChain::Contracts::ContractManager>   contract_manager_;
        std::unique_ptr<ExtraChain::Contracts::ToolchainRegistry> toolchain_registry_;
        std::unique_ptr<ExtraChain::Consensus::ConsensusService>  consensus_service_;
        std::mutex                                                pending_contracts_mutex_;
        std::unordered_map<std::string, ExtraChain::Contracts::PreparedContractChange> pending_contracts_;
        std::unique_ptr<ExtraChain::Core::NetworkRuntime>                              runtime_;
        std::unique_ptr<SerialExecutor>                                                serial_executor_;
        std::shared_ptr<ExtraChain::Core::DeadlineTask>                                dag_sync_timer_;
        std::shared_ptr<ExtraChain::Core::DeadlineTask>                                dag_peer_info_timer_;
        std::shared_ptr<ExtraChain::Core::DeadlineTask>                                backfill_timer_;
        std::shared_ptr<ExtraChain::Core::DeadlineTask>                                ready_timer_;
        std::shared_ptr<ExtraChain::Core::PeriodicTask>                                reward_timer_;
        std::shared_ptr<ExtraChain::Core::PeriodicTask>                                info_timer_;
        std::shared_ptr<ExtraChain::Core::PeriodicTask>                                luminance_timer_;
        std::vector<boost::signals2::scoped_connection>                                core_connections_;
        Event<>                                                                        initialized_event_;
        Event<>                                                                        ready_event_;
        Event<const ActorId&, const std::string&>                                      actor_renamed_event_;
        Event<>                                                                        actor_renames_loaded_event_;
        Event<const ActorId&, const std::string&>                                      subscription_added_event_;
        Event<RuntimeActivity> runtime_activity_changed_event_;

        void stop_runtime_tasks();
        void release_core();
        void schedule_dag_peer_info_collection(std::chrono::milliseconds delay);
        void cancel_dag_peer_info_collection();

        bool                                started_               = false;
        bool                                is_client_application_ = false;
        std::function<void()>               cleanup_callback_ = nullptr;

        std::optional<SubscriptionRow> subscription_row_;

        std::string                                   renames_file_id_waiting_;
        std::unordered_map<ActorId, std::string>      renames_todo_;
        std::string                                   node_identifier_;
        std::string                                   application_version_;
        std::string                                   bind_address_;
        RuntimeProfile                                runtime_profile_;
        std::atomic<RuntimeActivity>                  runtime_activity_ { RuntimeActivity::Foreground };
        std::atomic_bool                              core_released_ { false };
        std::vector<Actor<KeyPublic>>                 actors_broadcast_;
        std::set<std::pair<std::string, std::string>> identifiers_after_actors_sync_;

        std::uint16_t ws_port_;

    public:
        static constexpr const char* CHANNELS_VECTOR_NAME = "Channels";

        explicit ExtraChainNode(bool                          is_client_application = false,
                                bool                          is_custom_app         = false,
                                std::uint16_t                 port                  = 17593,
                                std::optional<RuntimeProfile> runtime_profile       = std::nullopt,
                                std::string                   application_version   = {},
                                std::string                   bind_address          = {});
        virtual ~ExtraChainNode();

        bool create_new_network(const std::string& login, const std::string& password);
        void queue_actor_broadcast(Actor<KeyPublic> actor);
        bool create_new_dag();
        bool create_usernames_vector();
        bool create_chat_templates();
        bool create_subscription_template();
        bool create_token_template();
        bool create_token_vector();
        bool create_token_allocations();
        void backfill_token_allocations();
        bool create_renames_template();
        //
        DfsFileStatus create_channels_vector();
        DfsFileStatus create_renames_vector();

        bool create_file_id_template(Dfs::FileIdState with_state = Dfs::FileIdState::Without);
        bool create_file_id_vector(const std::string& vector_name,
                                   Dfs::FileIdState   with_state = Dfs::FileIdState::Without);

        bool write_actor_rename(const ActorId& actor_id, const std::string& name);
        std::vector<std::pair<ActorId, std::string>> read_actor_renames();

        // not only for the one
        bool            create_subscription_vector(const std::string& file_name);
        void            start();
        bool            is_client_application() const;
        RuntimeProfile  runtime_profile() const;
        RuntimeActivity runtime_activity() const;
        [[nodiscard]] const std::string& bind_address() const noexcept;

        [[nodiscard]] bool                                       info_timer_active() const;
        RuntimeLimits                                            runtime_limits() const;
        void                                                     set_runtime_activity(RuntimeActivity activity);
        [[nodiscard]] boost::asio::any_io_executor               runtime_executor();
        [[nodiscard]] boost::asio::any_io_executor               storage_executor();
        [[nodiscard]] boost::asio::any_io_executor               compute_executor();
        [[nodiscard]] boost::asio::any_io_executor               serial_executor();
        [[nodiscard]] bool                                       on_serial_executor() const noexcept;
        [[nodiscard]] ExtraChain::Core::NetworkRuntime&          network_runtime();
        [[nodiscard]] Event<RuntimeActivity>&                    runtime_activity_event();
        [[nodiscard]] Event<>&                                   initialized_event() noexcept;
        [[nodiscard]] Event<>&                                   ready_event() noexcept;
        [[nodiscard]] Event<const ActorId&, const std::string&>& actor_renamed_event() noexcept;
        [[nodiscard]] Event<>&                                   actor_renames_loaded_event() noexcept;
        [[nodiscard]] Event<const ActorId&, const std::string&>& subscription_added_event() noexcept;

        template <typename Function>
        void post_storage(Function&& function) {
            boost::asio::post(storage_executor(), std::forward<Function>(function));
        }

        template <typename Function>
        void post_compute(Function&& function) {
            boost::asio::post(compute_executor(), std::forward<Function>(function));
        }


        Dag*                    dag() const;
        StateProjectionSnapshot state_projection() const;
        virtual NetworkService* network() const;
        ExtraChain::Consensus::ConsensusService* consensus() const;
        LuminanceManager*       luminance_manager() const;
        AccountController*      account_controller() const;
        ActorIndex*             actor_index() const;
        virtual DfsService*     dfs() const;
        DfsService*             dfs_service() const;
        DataMiningManager*      data_mining_manager() const;

        void start_mining();
        void stop_mining();

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

        std::string generate_node_identifier();
        std::string node_identifier();

        TokenManager*                             token_manager() const;
        ExtraChain::Contracts::ContractManager*   contract_manager() const;
        ExtraChain::Contracts::ToolchainRegistry* toolchain_registry() const;
        bool                                      stage_contract_change(std::string                                   transaction_hash,
                                                                        ExtraChain::Contracts::PreparedContractChange change);
        void finalize_contract_change(std::string_view transaction_hash, bool approved);
        std::expected<Transaction, TransactionError> send_contract_transaction(
            Transaction                                   transaction,
            const Actor<KeyPrivate>&                      signer,
            ExtraChain::Contracts::PreparedContractChange change);
        TransactionProveError validate_contract_transaction(const Transaction& transaction,
                                                            bool               stage_change = true);
        std::expected<Transaction, ExtraChain::Contracts::ContractFailure> submit_contract_deploy(
            std::string                   kind,
            std::span<const std::uint8_t> module,
            std::span<const std::uint8_t> init_arguments);
        std::expected<Transaction, ExtraChain::Contracts::ContractFailure> submit_contract_call(
            const ActorId&                               contract_id,
            std::string_view                             method,
            std::span<const std::uint8_t>                arguments,
            const ExtraChain::Contracts::VerifiedInputs& verified_inputs = {});
        std::expected<Transaction, ExtraChain::Contracts::ContractFailure> submit_contract_call(
            const Actor<KeyPrivate>&                     signer,
            const ActorId&                               contract_id,
            std::string_view                             method,
            std::span<const std::uint8_t>                arguments,
            const ExtraChain::Contracts::VerifiedInputs& verified_inputs = {});
        std::expected<Transaction, ExtraChain::Contracts::ContractFailure> submit_legacy_token_import(
            const Actor<KeyPrivate>&        signer,
            const ActorId&                  contract_id,
            std::span<const std::uint8_t>   arguments,
            const LegacyTokenMigrationData& migration);
        std::expected<Transaction, ExtraChain::Contracts::ContractFailure> submit_contract_upgrade(
            const ActorId&                contract_id,
            std::span<const std::uint8_t> module,
            std::span<const std::uint8_t> migration_arguments);
        std::expected<ExtraChain::Contracts::ContractReceipt, ExtraChain::Contracts::ContractFailure>
                                                   query_contract(const ActorId&                contract_id,
                                                                  std::string_view              method,
                                                                  std::span<const std::uint8_t> arguments);
        ExtraChain::Contracts::ContractCatalogPage list_contracts(
            const ExtraChain::Contracts::ContractCatalogFilter& filter = {});
        void set_cleanup_callback(std::function<void()> callback);
        bool is_custom_app_;

        ChatManager*  chat_manager();
        ThothManager* thoth_manager();
        JanusManager* janus_manager();

        std::expected<Transaction, TransactionError> add_subscription(const ActorId&     owner_id,
                                                                      const std::string& file_id,
                                                                      int                type,
                                                                      bool               auto_renew,
                                                                      const TokenId&     token_id);

    protected:
        virtual std::unique_ptr<DfsService>     create_dfs_service();
        virtual std::unique_ptr<NetworkService> create_network_service();
        [[nodiscard]] std::uint16_t configured_port() const noexcept;

    private:
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

    private:
        void timer_reward_request();
        void timer_luminance_autoremove();
        void timer_info_print();

    public:
        void selfTxInitContractAdded(const Transaction& transaction);
        void selfTxRepeatableAdded(const Transaction& transaction);

    public:
        void notification_token(std::string os, std::string actor_id, std::string token);

        void calculateBlockCount();
        void cleanUp();
        void process();

        void dagTimerStarting(int ms);
        void dagTimerStoping();
        void dagTimerTick();
        void dagPeerInfoTimerTick();
        void dagWatchdogTick();
        void dagSyncCheck();

        friend class ::Dag;
    };

} // namespace ExtraChain::Core
