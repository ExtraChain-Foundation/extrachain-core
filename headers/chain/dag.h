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
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include <boost/describe.hpp>

#include "utils/bignumber.h"
#include "chain/transaction.h"
#include "chain/transaction_cache.h"
#include "chain/dag_cache.h"
#include "chain/chain_index.h"
#include "chain/control_index.h"
#include "chain/pack_registry.h"
#include "chain/hot_section_store.h"
#include "runtime/event.h"

#include "3rdparty/rustex.h"

namespace ExtraChain::Core {
    class ExtraChainNode;
}
class Responder;

// Control hashes live on every CONTROL_INTERVAL-th section (section_id % 20 == 0).
// They anchor the chain so peers can verify long histories without replaying every tx.
static const SectionId CONTROL_INTERVAL      = SectionId(20);
static const int       CONTROL_INTERVAL_MOD  = 20;
static const SectionId CONTROL_INTERVAL_DIFF = CONTROL_INTERVAL - 1; // 19

// Sections this far behind the tip stay in the mutable hot store before
// being sealed into an immutable pack. Covers Light client's 15-section cache
// lag, control-search backoff (~37), and a buffer for late-arriving sync data
// or modest reorgs. 200 == 10 control intervals — cheap on disk (~400KB).
static constexpr int HOT_PACK_LAG = 200;
// Keep at most two not-yet-packed ranges in memory. The hot store remains the
// source of truth, so dropping a cache entry only causes a later database read.
static constexpr std::size_t   PACK_HOT_CACHE_LIMIT     = Pack::SECTIONS_PER_PACK * 2;
static constexpr std::size_t   PACK_SYNC_MAX_PACKS      = 100000;
static constexpr std::uint64_t PACK_SYNC_MAX_PACK_BYTES = 512ULL * 1024ULL * 1024ULL;

static inline bool transaction_section_is_open(const SectionId &frontier, const SectionId &section) {
    if (section > frontier) {
        return section - frontier <= SectionId(CACHE_LAG_SECTIONS);
    }
    if (frontier < SectionId(CACHE_LAG_SECTIONS)) {
        return true;
    }
    return section > frontier - SectionId(CACHE_LAG_SECTIONS);
}

// find_last_control() walks backwards from the current tip; these caps stop the walk
// once enough evidence accumulates that no control is ever coming:
//   - CONTROL_SEARCH_SKIP_LIMIT: sections scanned without finding a control. 37
//     is deliberately > 20 so we always cross at least one expected control slot.
//   - CONTROL_SEARCH_MISS_LIMIT: control-aligned sections that were missing
//     entirely (slot at % 20 == 0 but no section file). Hints at a broken chain.
static constexpr int CONTROL_SEARCH_SKIP_LIMIT = 37;
static constexpr int CONTROL_SEARCH_MISS_LIMIT = 10;

// helpers
static inline bool is_aligned20(const SectionId &s) {
    return (s % CONTROL_INTERVAL) == 0;
}
static inline SectionId align_down20(const SectionId &s) {
    SectionId m = s % CONTROL_INTERVAL;
    return m == 0 ? s : (s - m);
}
static inline SectionId max_sid(const SectionId &a, const SectionId &b) {
    return (a < b) ? b : a;
}

static inline SectionId control_interval_end(const SectionId &start) {
    return start == SectionId(0) ? SectionId(0) : start + CONTROL_INTERVAL_DIFF;
}

static inline bool control_interval_is_closed(const SectionId &start,
                                              const SectionId &chain_tip,
                                              const SectionId &cache_tip) {
    const auto end = control_interval_end(start);
    return end <= chain_tip && end <= cache_tip;
}

// generate control sections [from..to] with step 20
static inline std::vector<SectionId> control_ids_in(SectionId from, SectionId to) {
    std::vector<SectionId> v;
    for (SectionId s = from; s <= to; s += CONTROL_INTERVAL_MOD)
        v.push_back(s);
    return v;
}

/**
 * @brief Represents a section in the chain
 *
 * A section contains a collection of transactions and metadata
 * including its id and timestamp.
 */
struct Section {
    SectionId                  id;
    std::set<Transaction>      transactions;
    std::optional<std::string> control; // hash, interval 1-20, 21-40, ..

    /**
     * @brief Get all previous transaction hashes referenced by transactions in this section
     *
     * @return std::set<std::string> Set of previous transaction hashes
     */
    std::set<std::string> prev_hashs() const;

    /**
     * @brief hashs
     * @return
     */
    std::set<std::string> hashs() const;

    /**
     * @brief middle
     * @return
     */
    std::uint64_t middle() const;

    /**
     * @brief calculate_hash
     * @return
     */
    std::string calculate_hash() const;
};
BOOST_DESCRIBE_STRUCT(Section, (), (transactions, control))

struct SectionDiff {
    std::vector<Transaction> added_transactions;
    std::vector<Transaction> removed_transactions;
    std::vector<Transaction> modified_transactions;
};
BOOST_DESCRIBE_STRUCT(SectionDiff, (), (added_transactions, removed_transactions, modified_transactions))

/**
 * @brief Represents the result of a transaction validation
 *
 * Contains the transaction hash and the result of validation
 */
struct TransactionResult {
    SectionId             section_id;
    std::string           hash;
    TransactionProveError result;
};
BOOST_DESCRIBE_STRUCT(TransactionResult, (), (section_id, hash, result))

/**
 * @brief Represents the range of sections in the chain
 *
 * Contains the IDs of the first and last sections, as well as
 * the ID of the last cached section
 */
struct SectionRange {
    std::string first;       // id of the first saved section
    std::string last;        // id of the current (last) section
    std::string last_cached; // id of the last cached section
};
BOOST_DESCRIBE_STRUCT(SectionRange, (), (first, last, last_cached))

struct DagControl {
    SectionId   section_id;
    std::string control;
};
BOOST_DESCRIBE_STRUCT(DagControl, (), (section_id, control))

struct SectionSync {
    SectionId               to;
    std::set<Transaction>   txs;
    std::vector<DagControl> controls; // need map?
    SectionId               last_section;
};
BOOST_DESCRIBE_STRUCT(SectionSync, (), (to, txs, controls))

struct SectionFileData {
    SectionId   section_id;
    std::string file_bytes;
};
BOOST_DESCRIBE_STRUCT(SectionFileData, (), (section_id, file_bytes))

struct FileSectionsSync {
    SectionId                    to;
    std::vector<SectionFileData> sections;
    SectionId                    last_section;
};
BOOST_DESCRIBE_STRUCT(FileSectionsSync, (), (to, sections, last_section))

struct HashInterval {
    SectionId   from;
    SectionId   to;
    std::string hash;
};
BOOST_DESCRIBE_STRUCT(HashInterval, (), (from, to, hash))

struct DagTransactionBatch {
    std::vector<Transaction> transactions;
};
BOOST_DESCRIBE_STRUCT(DagTransactionBatch, (), (transactions))

// Pack-sync messages (between dag_version >= 100 peers).
struct PackInfo {
    std::uint64_t pack_id;
    SectionId     first_section;
    SectionId     last_section;
};
BOOST_DESCRIBE_STRUCT(PackInfo, (), (pack_id, first_section, last_section))

struct PackList {
    std::vector<PackInfo> packs;
};
BOOST_DESCRIBE_STRUCT(PackList, (), (packs))

struct PackRequest {
    std::uint64_t pack_id;
    std::uint64_t offset = 0; // byte offset of the requested chunk
};
BOOST_DESCRIBE_STRUCT(PackRequest, (), (pack_id, offset))

struct PackData {
    std::uint64_t pack_id;
    std::uint64_t offset     = 0; // byte offset of this chunk in the pack
    std::uint64_t total_size = 0; // full pack size, so the receiver knows the end
    std::string   bytes;          // one chunk of the .pack file
};
BOOST_DESCRIBE_STRUCT(PackData, (), (pack_id, offset, total_size, bytes))

// Legacy balance-cache snapshot messages. Current nodes reject this derived
// state and rebuild it from verified local sections.
struct CacheBalanceRow {
    std::string actor_id;
    std::string token_id;
    std::string balance;
};
BOOST_DESCRIBE_STRUCT(CacheBalanceRow, (), (actor_id, token_id, balance))

struct CacheSnapshot {
    SectionId                    section; // cached_section the balances are valid at
    std::vector<CacheBalanceRow> balances;
};
BOOST_DESCRIBE_STRUCT(CacheSnapshot, (), (section, balances))

/**
 * @brief Enumeration of chain synchronization states
 */
enum class DagSyncStatus {
    None,     // No synchronization in progress
    LastInfo, // Retrieving last chain info from peers
    Sections  // Synchronizing sections
};

/**
 * @brief Enumeration of the DAG operational states
 */
enum class DagStatus {
    Started, // Dag has been initialized
    Ready,   // Dag is operational and ready for transactions
    Final,   // Dag is processing final operations
    Sync,    // Dag is synchronizing with the network
    Maybe,   // Dag is in a potential sync state
    Timered, // Dag is processing timed operations
};

enum class WriteResult {
    Write,
    NoChanges
};

/**
 * @brief Information about the last state of the chain
 *
 * Contains the ID of the last section, its previous hashes,
 * and the timestamp of the genesis section / transaction
 */
struct DagLastInfo {
    SectionId     last_section_id;
    SectionId     last_control_section_id;
    std::string   last_control_hash;
    std::uint64_t zero_date;
    DagStatus     status;
};
BOOST_DESCRIBE_STRUCT(DagLastInfo,
                      (),
                      (last_section_id, last_control_hash, last_control_section_id, zero_date, status))

/**
 * @brief Package of data for light mode synchronization
 *
 * Contains cached balances, the ID of the cached section,
 * and transactions needed for light mode synchronization
 */
struct DagLightPackage {
    Balances                                       cache;         // Cached account balances
    SectionId                                      cache_section; // Id of the section corresponding to the cache
    std::set<Transaction>                          txs;           // Transactions since the cached section
    std::vector<std::pair<SectionId, std::string>> controls;      // Control hashs
};
BOOST_DESCRIBE_STRUCT(DagLightPackage, (), (cache, cache_section, txs, controls))

struct DagControlRangeRequest {
    SectionId from;
    SectionId to; // to >= from
};
BOOST_DESCRIBE_STRUCT(DagControlRangeRequest, (), (from, to))

struct DagControlRangeResponse {
    SectionId               from;
    SectionId               to;
    std::vector<DagControl> controls;
};
BOOST_DESCRIBE_STRUCT(DagControlRangeResponse, (), (from, to, controls))

/**
 * @brief Directed Acyclic Chain implementation
 *
 * This class manages the chain storage, transaction processing,
 * and network synchronization. It supports both full and light modes
 * of operation.
 */
class Dag {
public:
    using AdmissionCompletion =
        std::function<void(std::expected<void, TransactionProveError> result, bool should_forward)>;
    using StatusEvent      = ExtraChain::Core::Event<DagStatus>;
    using SyncStartEvent   = ExtraChain::Core::Event<SectionId, SectionId>;
    using SectionEvent     = ExtraChain::Core::Event<SectionId>;
    using TimerEvent       = ExtraChain::Core::Event<int>;
    using TransactionEvent = ExtraChain::Core::Event<SectionId, const std::string &>;

    /**
     * @brief Construct a new Dag object
     *
     * @param node Pointer to the ExtraChainNode that owns this DAG
     */
    Dag(ExtraChain::Core::ExtraChainNode *node);

    ~Dag();

    [[nodiscard]] StatusEvent               &status_event() noexcept;
    [[nodiscard]] SyncStartEvent            &sync_start_event() noexcept;
    [[nodiscard]] SectionEvent              &sync_progress_event() noexcept;
    [[nodiscard]] ExtraChain::Core::Event<> &sync_finish_event() noexcept;
    [[nodiscard]] TimerEvent                &timer_start_event() noexcept;
    [[nodiscard]] ExtraChain::Core::Event<> &timer_stop_event() noexcept;
    [[nodiscard]] TransactionEvent          &transaction_sent_event() noexcept;
    [[nodiscard]] TransactionEvent          &transaction_approved_event() noexcept;
    [[nodiscard]] TransactionEvent          &transaction_rejected_event() noexcept;
    [[nodiscard]] ExtraChain::Core::Event<> &control_started_event() noexcept;
    [[nodiscard]] ExtraChain::Core::Event<> &control_ended_event() noexcept;
    [[nodiscard]] SectionEvent              &control_progress_event() noexcept;
    [[nodiscard]] ExtraChain::Core::Event<> &control_search_started_event() noexcept;
    [[nodiscard]] ExtraChain::Core::Event<> &control_search_ended_event() noexcept;

    /**
     * @brief Get the current section ID
     *
     * @return SectionId The current (latest) section ID
     */
    SectionId current_section() const;

    /**
     * @brief set_current_section
     * @param new_current_section
     */
    void set_current_section(const SectionId &new_current_section);

    /**
     * @brief Get the current DAG operation mode
     *
     * @return DagMode The current mode (Full or Light)
     */
    DagMode mode() const;

    /**
     * @brief Get the current operational status of the DAG
     *
     * @return DagStatus The current status
     */
    DagStatus status() const;

    /**
     * @brief Set the DAG operation mode
     *
     * @param mode The new operation mode
     */
    void set_mode(DagMode mode);

    /**
     * @brief Switch from Light to Full mode on-the-fly
     *
     * Sets status to Sync, changes mode to Full, clears Light data,
     * and starts full chain synchronization. No-op if already in Full mode.
     */
    void force_full_mode();
    void force_light_mode();

    /**
     * @brief Set the DAG operational status
     *
     * @param status The new status
     */
    void set_status(DagStatus status);

    /**
     * @brief Get the transaction cache
     *
     * @return TransactionCache& Reference to the transaction cache
     */
    TransactionCache &transaction_cache();

    /**
     * @brief Get the balance cache
     *
     * @return DagCache& Reference to the balance cache
     */
    DagCache &cache();

    /**
     * @brief Persistent transaction index used for wallet/explorer queries
     *        and fast duplicate hash checks.
     */
    ChainIndex       *chain_index();
    const ChainIndex *chain_index() const;
    bool              chain_index_enabled() const;

    /**
     * @brief Get the ID of the first saved section
     *
     * @return SectionId The first saved section ID
     */
    SectionId first_saved_section();

    /**
     * @brief file_section
     * @param section
     * @return
     */
    SectionId file_section(const SectionId &section) const;

    /**
     * @brief Get the folder path for a section
     *
     * @param section The section ID
     * @return std::string The folder path
     */
    std::string file_folder(const SectionId &section) const;

    /**
     * @brief Get the file path for a section
     *
     * @param section The section ID
     * @return std::string The file path
     */
    std::string file_path(const SectionId &section) const;

    /**
     * @brief Prepare a transaction for submission
     *
     * Sets the section ID, previous hashes, and signs the transaction.
     *
     * @param transaction The transaction to prepare
     * @param signer The actor who will sign the transaction
     * @param ignore_zero Param for dag genesis
     * @return std::expected<Transaction, TransactionError> The prepared transaction or an error
     */
    std::expected<Transaction, TransactionError> prepare_transaction(const Transaction       &transaction,
                                                                     const Actor<KeyPrivate> &signer,
                                                                     bool                     ignore_zero = false);

    /**
     * @brief Send a transaction to the network
     *
     * Prepares the transaction and broadcasts it to the network.
     *
     * @param transaction The transaction to send
     * @param signer The actor who will sign the transaction
     * @return std::expected<Transaction, TransactionError> The sent transaction or an error
     */
    std::expected<Transaction, TransactionError> send_transaction(const Transaction       &transaction,
                                                                  const Actor<KeyPrivate> &signer);

    /**
     * @brief Process a transaction received from the network
     *
     * Validates and stores the transaction if valid.
     *
     * @param transaction The transaction to process
     * @param responder The responder to send the result to
     * @return std::expected<void, bool> Success or failure
     */
    std::expected<void, TransactionProveError> network_transaction(const Transaction &transaction,
                                                                   const Responder   &responder);
    bool                                       submit_network_transaction(const Transaction  &transaction,
                                                                          const Responder    &responder,
                                                                          AdmissionCompletion completion);
    void                                       flush_admission();

    /**
     * @brief Process a transaction validation result from the network
     *
     * Updates the local state based on transaction validation results.
     *
     * @param tx_result The tx_result of the transaction
     * @param result The validation result
     */
    void network_transaction_result(const TransactionResult &tx_result, const Responder &responder);

    /**
     * @brief Process a section received from the network
     *
     * @param section The section to process
     */
    void network_section(const Section &section);

    /**
     * @brief Calculate balances for a set of actors
     *
     * @param actor_ids Vector of actor IDs to calculate balances for
     * @param token_id The token ID to calculate balances for
     * @return std::unordered_map<ActorId, BigNumberFloat> Map of actor IDs to balances
     */
    Balances calculate_actors_balance(const std::vector<ActorId> &actor_ids,
                                      std::optional<SectionId>    to_section = std::nullopt);

    void invalidate_token_allocations();

    /**
     * @brief Add a transaction to the sent transactions list
     *
     * @param transaction The transaction that was sent
     */
    void add_transaction_sended(const Transaction &transaction);

    /**
     * @brief Update the chain range information
     *
     * Updates the persistent storage with the current first section,
     * last section, and last cached section IDs.
     * @param allow_lower_first Permit a lower `first` than on disk (e.g. installing cold packs extends history
     * backwards).
     */
    void update_range(bool allow_lower_first = false);

    /**
     * @brief Search for a transaction by its hash
     *
     * @param section Section the tx lives in. Required because Transaction::calculate_hash
     *                mixes section_id into the hash, so each section has its own hash space.
     * @param hash    Hash within that section.
     * @return std::optional<Transaction> The transaction if found, or nullopt
     */
    std::optional<Transaction> find_transaction(const SectionId &section, const std::string &hash) const;

    std::optional<std::pair<SectionId, std::string>> search_duplicate_by_sender(const ActorId &actor_id,
                                                                                std::uint64_t  latest_timestamp,
                                                                                std::uint64_t  time) const;

    // TODO: search to future

    /**
     * @brief Read a section from storage
     *
     * @param section_id The ID of the section to read
     * @return std::optional<Section> The section if found, or nullopt
     */
    std::optional<Section> read_section(const SectionId &section_id) const;

    /**
     * @brief exists_section_file
     * @param section_id
     * @return
     */
    bool exists_section_file(const SectionId &section_id) const;

    /**
     * @brief Start chain synchronization
     *
     * Initiates the process of synchronizing with the network.
     */
    void start_sync();

    /**
     * @brief Start chain verification
     *
     * Initiates the process of verifying the chain against the network.
     */
    void start_check();

    /**
     * @brief Handle a sync status request from the network
     *
     * @param responder The responder to send the status to
     */
    void network_status_sync_request(const Responder &responder);

    /**
     * @brief Process a sync status response from the network
     *
     * @param last_info The chain info received
     * @param responder The responder that sent the info
     */
    void network_status_sync_response(const DagLastInfo &last_info, const Responder &responder);

    /**
     * @brief Request specific sections from the network
     *
     * @param from The starting section ID
     * @param to The ending section ID
     * @param responder The responder to send the request to
     */
    void network_request_sections(const SectionId &from, const SectionId &to, const Responder &responder);

    /**
     * @brief Process a sections response from the network
     *
     * @param compressed The compressed sections data
     * @param responder The responder that sent the data
     */
    void network_request_sections_response(const std::string &compressed, const Responder &responder);

    void network_request_file_sections(const SectionId &from, const SectionId &to, const Responder &responder);
    void network_file_sections_response(const std::string &compressed, const Responder &responder);

    // Pack-level sync (peers with dag_version >= 100 only).
    // Server side: respond to peer's queries about our packs.
    void network_pack_list_request(const Responder &responder);
    void network_pack_request(const PackRequest &req, const Responder &responder);
    // Client side: peer told us what packs it has / sent a pack we asked for.
    void network_pack_list_response(const PackList &list, const Responder &responder);
    void network_pack_data_response(const PackData &data, const Responder &responder);

    // Initiate pack-level sync against a single peer (the same one currently
    // selected by the existing sync flow). No-op for legacy peers.
    void start_pack_sync(const Responder &responder);

    // Balance-cache snapshot transfer (peers with dag_version >= 100 only).
    // Server side: hand over our prebuilt balance cache.
    void network_cache_snapshot_request(const Responder &responder);
    // Client side: install a received snapshot instead of rebuilding locally.
    void network_cache_snapshot_response(const std::string &compressed, const Responder &responder);
    // Ask the sync peer for its cache snapshot (called once file-sync completes).
    void request_cache_snapshot(const Responder &responder);

    /**
     * @brief Request light mode data from the network
     *
     * Requests cached balances and recent transactions for light mode operation.
     *
     * @param responder The responder to send the request to
     */
    void network_request_light(const Responder &responder);

    /**
     * @brief Process light mode data received from the network
     *
     * Stores cached balances and processes recent transactions for light mode operation.
     *
     * @param dag_light The light mode data package
     * @param responder The responder that sent the data
     */
    void network_response_light(const DagLightPackage &dag_light, const Responder &responder);

    /**
     * @brief network_hash_interval
     * @param hash_interval
     * @param responder
     */
    void network_hash_interval(const HashInterval &hash_interval, const Responder &responder);

    /**
     * @brief Set the chain synchronization status
     *
     * @param status The new sync status
     */
    void set_sync_status(DagSyncStatus status);

    /**
     * @brief Process transactions that were cached during synchronization
     *
     * Processes transactions that were received while the chain
     * was synchronizing with the network.
     */
    void process_cached_transactions(bool not_ready = false);
    void retry_contract_transactions();
    void request_contract_section(const SectionId &section_id);

    std::unordered_map<std::string, Transaction> sended_transactions() {
        return sended_transactions_;
    }

    std::unordered_map<std::string, Transaction> failed_transactions() {
        return failed_transactions_;
    }

    size_t sended_transactions_size() const {
        return sended_transactions_.size();
    }
    size_t failed_transactions_size() const {
        return failed_transactions_.size();
    }
    size_t last_txs_size() const {
        std::lock_guard lock(last_txs_mutex_);
        return last_txs_.size();
    }
    size_t cached_txs_size() {
        auto g = cached_txs_.lock();
        return g->size();
    }
    bool should_queue_network_transaction();

    /**
     * @brief Begin accepting sync and network messages; start sync timers.
     *        Idempotent — calling twice is a no-op.
     *        Call after construction (and after any migration) so the caller
     *        can decide when the node is ready to join the network.
     */
    void start();

    /**
     * @brief Stop accepting network messages, halt timers, finish in-flight work.
     *        Storage stays openable — a later start() resumes from where we stopped.
     *        Safe to call before destruction, before mode change, or before wipe.
     */
    void stop();

    /**
     * @brief Whether the Dag is currently accepting incoming network messages.
     *        Network handlers should consult this and drop messages when false
     *        to avoid racing against shutdown.
     */
    bool is_accepting_messages() const;

private:
    struct AdmissionState;
    struct TransactionValidationFacts {
        bool                hash_valid = false;
        std::optional<bool> sender_exists;
        std::optional<bool> receiver_exists;
        std::optional<bool> signature_valid;
    };
    struct DeferredContractTransaction {
        Transaction                transaction;
        std::shared_ptr<Responder> responder;
    };
    using DeferredContractMap = std::unordered_map<std::string, DeferredContractTransaction>;
    std::shared_ptr<AdmissionState> admission_state_;

    static std::shared_ptr<AdmissionState> create_admission_state(Dag *owner);
    static bool                            is_admission_worker();
    void                                   set_admission_accepting(bool accepting);

    std::expected<void, TransactionProveError> network_transaction_immediate(const Transaction &transaction,
                                                                             const Responder   &responder);
    TransactionProveError                      prove_transaction_with_facts(const Transaction                &tx,
                                                                            const std::set<Transaction>      &transactions,
                                                                            const std::set<Transaction>      *pending_transactions,
                                                                            const SectionId                  *validation_frontier,
                                                                            const TransactionValidationFacts *facts);

    StatusEvent                                           status_event_;
    SyncStartEvent                                        sync_start_event_;
    SectionEvent                                          sync_progress_event_;
    ExtraChain::Core::Event<>                             sync_finish_event_;
    TimerEvent                                            timer_start_event_;
    ExtraChain::Core::Event<>                             timer_stop_event_;
    TransactionEvent                                      transaction_sent_event_;
    TransactionEvent                                      transaction_approved_event_;
    TransactionEvent                                      transaction_rejected_event_;
    ExtraChain::Core::Event<>                             control_started_event_;
    ExtraChain::Core::Event<>                             control_ended_event_;
    SectionEvent                                          control_progress_event_;
    ExtraChain::Core::Event<>                             control_search_started_event_;
    ExtraChain::Core::Event<>                             control_search_ended_event_;
    ExtraChain::Core::ExtraChainNode                     *node;               // Parent node reference
    TransactionCache                                      transaction_cache_; // Transaction cache for fast lookups
    std::unordered_map<std::string, Transaction>          sended_transactions_; // Transactions sent but not yet
    std::unordered_map<std::string, Transaction>          failed_transactions_; // Transactions failed
    std::unordered_map<NodeId, std::uint64_t>             last_txs_;
    mutable std::mutex                                    last_txs_mutex_;
    std::mutex                                            token_allocations_mutex_;
    ActorId                                               token_allocations_owner_;
    std::optional<std::string>                            token_allocations_file_id_;
    bool                                                  token_allocations_cache_loaded_ = false;
    std::map<std::pair<ActorId, TokenId>, BigNumberFloat> token_allocations_cache_;
    DagCache                                              cache_; // Balance cache for fast calculations

    mutable std::shared_mutex    section_mutex_; // Protects one storage operation.
    mutable std::recursive_mutex save_mutex_;    // Protects section read-modify-write cycles.
    mutable std::mutex           range_mutex_;   //
    std::optional<SectionRange>  persisted_range_;

    SectionId current_section_     = SectionId(-1);      // Current (latest) section ID
    SectionId first_saved_section_ = SectionId(-1);      // First section ID saved in the chain
    DagMode   mode_                = DagMode::Full;      // Current operation mode
    DagStatus status_              = DagStatus::Started; // Current operational status

    DagSyncStatus                                sync_status_  = DagSyncStatus::None; // Current sync status
    DagSyncStatus                                check_status_ = DagSyncStatus::None; // Current check status
    SectionId                                    sync_last_index_;                    // Last section index to sync
    int                                          requests_count_ = 0; // Number of outstanding requests
    int                                          min_req_count_  = 5;
    std::unordered_map<std::string, DagLastInfo> last_info_; // Last chain info from peers
    std::uint64_t                                timestamp_bigger_sync_start_ = 0;
    bool                                         search_control_              = false;
    bool                                         light_requested_             = false;

    rustex::mutex<std::set<Transaction>> cached_txs_; // Transactions cached during synchronization
    static constexpr std::size_t         MaxDeferredContractTransactions = 1024;
    std::mutex                           deferred_contracts_mutex_;
    DeferredContractMap                  deferred_contracts_;

    // Immutable packed storage for cold sections (10k per pack)
    std::unique_ptr<Pack::Registry>                pack_registry_;
    std::unique_ptr<HotSectionStore>               hot_section_store_;
    SectionId                                      next_pack_index_ = SectionId(0);
    std::mutex                                     pack_mutex_;
    std::mutex                                     pack_hot_cache_mutex_;
    std::mutex                                     pack_hot_completion_mutex_;
    std::condition_variable                        pack_hot_completion_;
    std::atomic_bool                               pack_hot_running_    = false;
    std::atomic_uint64_t                           pack_hot_generation_ = 0;
    std::map<SectionId, std::string>               pack_hot_cache_;
    std::mutex                                     file_sync_response_mutex_;
    std::optional<std::pair<SectionId, SectionId>> hot_gap_request_;
    std::recursive_mutex                           sync_last_info_mutex_;

    void continue_with_collected_peer_info();

    struct PendingSyncResponse {
        std::string message_id;
        SectionId   from;
        SectionId   to;
    };
    mutable std::mutex                 sync_response_request_mutex_;
    std::optional<PendingSyncResponse> pending_section_response_;
    std::optional<PendingSyncResponse> pending_file_response_;

    // Periodic "am I behind?" trigger. The worker only schedules work; all DAG state
    // is read and changed on the node serial executor.
    std::thread      watchdog_;
    std::atomic_bool watchdog_stop_requested_ = false;
    std::atomic_bool watchdog_tick_pending_   = false;
    std::atomic_bool sync_check_pending_      = false;
    // Height seen by the previous watchdog round, and how many rounds it has not moved
    // while a sync was supposedly running. Used to tell a slow sync from a stuck one.
    SectionId last_watchdog_section_ = SectionId(-1);
    int       stalled_sync_rounds_   = 0;

    // Boundary -> when we last refetched it after a control mismatch. Several peers
    // reporting the same disagreement must not each trigger their own refetch.
    mutable std::mutex                 refetched_intervals_mutex_;
    std::map<SectionId, std::uint64_t> refetched_intervals_;

    // Interval hashes from peers for boundaries we have not sealed yet: section -> hash.
    // Without this the claim is dropped and the verification never happens, because our
    // control appears a little later than the peer's. Bounded to the newest few.
    mutable std::mutex               pending_intervals_mutex_;
    std::map<SectionId, std::string> pending_intervals_;

    // Persistent tx index (by hash / sender / receiver / token / time).
    // Full mode: every tx. Light mode: only tx involving local wallets.
    std::unique_ptr<ChainIndex> chain_index_;
    bool                        chain_index_enabled_ = false;

    // Read-side accelerator for control hashes (section_id -> hash). Always on:
    // control lookups are on the sync hot path. Rebuildable, not consensus.
    std::unique_ptr<ControlIndex> control_index_;
    // Set once the control index has been populated for the loaded chain, so the
    // one-time lazy rebuild (first control lookup on a cold index) runs only once.
    std::atomic_bool control_index_ready_ = false;
    std::mutex       controls_generation_mutex_;

    // Populate the control index from disk on first use if it is cold. Idempotent
    // and cheap once warm. Called from control lookups so it is independent of
    // construction/load order.
    void ensure_control_index();
    void repair_control_chain();

    // Lifecycle flags:
    //   started_ — set by start(), cleared by stop(). Guards double-start.
    //   accepting_messages_ — true between start() and stop(); network
    //   handlers must check this to drop inbound traffic during shutdown.
    std::atomic<bool> started_ { false };
    std::atomic<bool> accepting_messages_ { false };

    //
    void                                           add_to_cached_tx(const Transaction &transaction);
    void                                           schedule_watchdog_tick();
    void                                           watchdog_tick();
    void                                           schedule_sync_check();
    void                                           sync_check();
    void                                           clear_pending_sync_responses();
    std::optional<std::pair<SectionId, SectionId>> pending_sync_range(const Responder &responder,
                                                                      const SectionId &to,
                                                                      bool             file_response) const;
    void consume_pending_sync_response(const Responder &responder, bool file_response);

    std::map<SectionId, Section> read_hot_sections(const SectionId &from, const SectionId &to) const;

    // Pack hot sections into an immutable pack when enough have accumulated.
    // Called from write_section when the hot range crosses a pack boundary.
    void try_pack_hot();
    void pack_hot_sections(const SectionId    &max_pack_index,
                           const SectionId    &first_saved_section,
                           const std::uint64_t generation);
    void finish_pack_hot();

    // Pack-sync state. One pack is installed at a time, with a bounded window
    // of chunk requests to avoid one network round trip per 256 KiB.
    std::mutex                        pack_sync_mutex_;
    std::vector<Pack::PackId>         pack_sync_pending_;
    bool                              pack_sync_in_flight_     = false;
    bool                              pack_sync_installed_any_ = false;
    Pack::PackId                      pack_sync_current_id_    = 0;
    std::uint64_t                     pack_sync_next_offset_   = 0;
    std::uint64_t                     pack_sync_total_size_    = 0;
    std::unordered_set<std::uint64_t> pack_sync_outstanding_offsets_;
    std::unordered_set<std::uint64_t> pack_sync_received_offsets_;
    std::string                       pack_sync_peer_;
    std::optional<SectionId>          pack_sync_fallback_from_;

    // Pull next pack from pack_sync_pending_ and send DagPackRequest.
    // Called after each pack is received (or after PackList arrives).
    void issue_next_pack_request(const Responder &responder);
    void issue_pack_window(const Responder &responder);
    bool validate_pack_controls(Pack::PackId id, const std::map<SectionId, Section> &sections) const;
    bool validate_received_pack(Pack::PackId id, const Pack::Reader &reader) const;

    std::optional<BigNumberFloat> frozen_token_allocation(const ActorId &actor, const TokenId &token);

    /**
     * @brief Request sections from the network
     *
     * @param from Starting section ID
     * @param to Ending section ID
     * @param responder Responder to send the request to
     */
    void request_sections(const SectionId &from, const SectionId &to, const Responder &responder);

    void request_file_sections(const SectionId &from, const SectionId &to, const Responder &responder);

    /**
     * @brief Send a sync request to the network
     *
     * Determines what to sync based on peer information and sends appropriate requests.
     */
    void handle_sync_request();

    /**
     * @brief Write a section to storage
     *
     * @param section The section to write
     * @return std::optional<bool> Success or failure
     */
    std::optional<bool> write_section(const Section &section);

    /**
     * @brief write_section_diff
     * @param section
     * @return
     */
    std::optional<std::pair<WriteResult, std::optional<SectionDiff>>> write_section_diff(const Section &section);

    /**
     * @brief write_control
     * @param section_id
     * @param hash
     * @return
     */
    std::optional<WriteResult> write_control(const SectionId &section_id, const std::string &hash);

    /**
     * @brief remove_control
     * @param section_id
     * @return
     */
    std::optional<WriteResult> remove_control(const SectionId &section_id);

    /**
     * @brief timer_tick
     */
    void timer_tick();

    /**
     * @brief calculate_section_diff
     * @param old_section
     * @param new_section
     * @return
     */
    SectionDiff calculate_section_diff(const Section &old_section, const Section &new_section);

public:
    /**
     * @brief Save a transaction to storage
     *
     * Creates a new section or adds to an existing section as needed.
     *
     * @param transaction The transaction to save
     * @return bool Success or failure
     */
    bool save_transaction(const Transaction &transaction);

    bool local_remove_transaction(const SectionId &section_id, const std::string &hash);

    /**
     * @brief Save multiple transactions to storage in batch
     *      * Groups transactions by section and processes each section once,
     * creating new sections or adding to existing sections as needed.
     * Maintains the same behavior as save_transaction but optimizes
     * by reducing I/O operations when multiple transactions belong
     * to the same section.
     * @param transactions Vector of transactions to save
     * @return bool True if all transactions were saved successfully, false otherwise
     */
    std::optional<std::pair<SectionId, SectionId>> save_transactions(const std::set<Transaction> &transactions);

    void check_self(const Transaction &transaction);

    /**
     * @brief Validate a transaction
     *
     * Checks if a transaction meets all validation rules.
     *
     * @param tx The transaction to validate
     * @param transactions The set of transactions in the current section
     * @return TransactionProveError Error code or NoError if valid
     */
    TransactionProveError prove_transaction(const Transaction           &tx,
                                            const std::set<Transaction> &transactions,
                                            const std::set<Transaction> *pending_transactions = nullptr,
                                            const SectionId             *validation_frontier  = nullptr);

    void clear_dag();
    void clear_dag_folder();

    void remove_sections(const SectionId &from);

    /**
     * @brief tx_list_log
     * @param actor_id
     */
    void tx_list_log(const ActorId &actor_id, bool ignore_reward = false);
    void mint_analysis_log();

    /**
     * @brief cache_log
     */
    void cache_log();

    /**
     * @brief sum
     * @return
     */
    std::map<TokenId, BigNumberFloat> sum();

    /**
     * @brief last_month
     * @return
     */
    std::set<ActorId> last_month();

    BigNumberFloat sum_all_rewards();

    /**
     * @brief find_last_control
     * @param from
     * @param disable_braek
     * @return
     */
    std::optional<DagControl> find_last_control(SectionId from = SectionId(-1), bool disable_break = false);

    /**
     * @brief read_control
     * @param section_id
     * @return
     */
    std::optional<DagControl> read_control(const SectionId &section_id);

    /**
     * @brief read_control_prev
     * @param section_id
     * @return
     */
    std::optional<DagControl> read_control_prev(const SectionId &section_id);

    /**
     * @brief read_control_next
     * @param section_id
     * @return
     */
    std::optional<DagControl> read_control_next(const SectionId &section_id);

    /**
     * @brief generate_hash_for_interval
     * @param start
     * @param last_hash
     * @return
     */
    std::optional<std::string> generate_hash_for_interval(const SectionId &start, std::string &last_hash);

    /**
     * @brief generate_hash_from_section
     * @param start
     * @param full_generation
     * @return
     */
    std::optional<std::string> generate_hash_from_section(const SectionId &start,
                                                          Force            full_generation = Force::None,
                                                          Force            qt_signals      = Force::Active);

    /**
     * @brief generate_hash
     * @param start_section
     * @return
     */
    bool generate_hash(const SectionId &start_section = SectionId(0), Force qt_signals = Force::Active);

    /**
     * @brief hash_interval
     * @param from
     * @param to
     * @return
     */
    std::optional<std::string> hash_interval(const SectionId &from, const SectionId &to);

    /**
     * @brief start_control
     */
    void start_control(Force force = Force::None, Force qt_signals = Force::Active);

    void clear_controls(const SectionId &from = SectionId(0));

    void clear_controls_async(const SectionId &from = SectionId(0));

    /**
     * @brief request_control_section
     * @param section_id
     * @param responder
     */
    void request_control_section(const SectionId &from_top, const Responder &responder);

    /**
     * @brief network_request_control_section
     * @param dag_control
     * @param responder
     */
    // void network_request_control_section(const DagControl &dag_control, const Responder &responder);
    void network_request_control_section(const DagControlRangeRequest &control_request,
                                         const Responder              &responder);

    void network_control_range_response(const DagControlRangeResponse &control_response,
                                        const Responder               &responder);

    friend class ExtraChain::Core::ExtraChainNode;
    friend class DagCache;
};
