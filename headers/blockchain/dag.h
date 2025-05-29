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

#include <boost/describe.hpp>

#include "utils/bignumber.h"
#include "blockchain/transaction.h"
#include "blockchain/transaction_cache.h"
#include "blockchain/dag_cache.h"

#include "3rdparty/rustex.h"

class ExtraChainNode;
class Responder;

/**
 * @brief Represents a section in the chain
 *
 * A section contains a collection of transactions and metadata
 * including its ID and timestamp.
 */
struct Section {
    BigNumber             id;
    std::set<Transaction> transactions;

    /**
     * @brief Get all previous transaction hashes referenced by transactions in this section
     *
     * @return std::set<std::string> Set of previous transaction hashes
     */
    std::set<std::string> prev_hashs();

    std::set<std::string> hashs();

    std::uint64_t middle();
};
BOOST_DESCRIBE_STRUCT(Section, (), (transactions))

/**
 * @brief Represents the result of a transaction validation
 *
 * Contains the transaction hash and the result of validation
 */
struct TransactionResult {
    std::string           hash;
    TransactionProveError result;
};
BOOST_DESCRIBE_STRUCT(TransactionResult, (), (hash, result))

/**
 * @brief Represents the range of sections in the chain
 *
 * Contains the IDs of the first and last sections, as well as
 * the ID of the last cached section
 */
struct SectionRange {
    std::string first;       // ID of the first saved section
    std::string last;        // ID of the current (last) section
    std::string last_cached; // ID of the last cached section
};
BOOST_DESCRIBE_STRUCT(SectionRange, (), (first, last, last_cached))

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
    Started, // DAG has been initialized
    Ready,   // DAG is operational and ready for transactions
    Final,   // DAG is processing final operations
    Sync,    // DAG is synchronizing with the network
    Maybe,   // DAG is in a potential sync state
    Timered, // DAG is processing timed operations
};

/**
 * @brief Information about the last state of the chain
 *
 * Contains the ID of the last section, its previous hashes,
 * and the timestamp of the genesis section / transaction
 */
struct DagLastInfo {
    BigNumber             last_section_id;
    std::set<std::string> last_hash;
    std::uint64_t         zero_date;
};
BOOST_DESCRIBE_STRUCT(DagLastInfo, (), (last_section_id, last_hash, zero_date))

/**
 * @brief Package of data for light mode synchronization
 *
 * Contains cached balances, the ID of the cached section,
 * and transactions needed for light mode synchronization
 */
struct DagLightPackage {
    Balances                 cache;         // Cached account balances
    BigNumber                cache_section; // ID of the section corresponding to the cache
    std::vector<Transaction> txs;           // Transactions since the cached section
};
BOOST_DESCRIBE_STRUCT(DagLightPackage, (), (cache, cache_section, txs))

/**
 * @brief Directed Acyclic Chain implementation
 *
 * This class manages the chain storage, transaction processing,
 * and network synchronization. It supports both full and light modes
 * of operation.
 */
class Dag {
public:
    /**
     * @brief Construct a new Dag object
     *
     * @param node Pointer to the ExtraChainNode that owns this DAG
     */
    Dag(ExtraChainNode *node);

    /**
     * @brief Get the current section ID
     *
     * @return BigNumber The current (latest) section ID
     */
    BigNumber current_section() const {
        return current_section_;
    }

    void set_current_section(const BigNumber &new_current_section);

    /**
     * @brief Get the current DAG operation mode
     *
     * @return DagMode The current mode (Full or Light)
     */
    DagMode mode() const {
        return mode_;
    }

    /**
     * @brief Get the current operational status of the DAG
     *
     * @return DagStatus The current status
     */
    DagStatus status() const {
        return status_;
    }

    /**
     * @brief Set the DAG operation mode
     *
     * @param mode The new operation mode
     */
    void set_mode(DagMode mode) {
        this->mode_ = mode;
    }

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
    TransactionCache &transaction_cache() {
        return transaction_cache_;
    }

    /**
     * @brief Get the balance cache
     *
     * @return DagCache& Reference to the balance cache
     */
    DagCache &cache() {
        return cache_;
    }

    /**
     * @brief Get the ID of the first saved section
     *
     * @return BigNumber The first saved section ID
     */
    BigNumber first_saved_section() {
        return first_saved_section_;
    }

    /**
     * @brief Get the folder path for a section
     *
     * @param section The section ID
     * @return std::string The folder path
     */
    std::string file_folder(const BigNumber &section) const;

    /**
     * @brief Get the file path for a section
     *
     * @param section The section ID
     * @return std::string The file path
     */
    std::string file_path(const BigNumber &section) const;

    /**
     * @brief Prepare a transaction for submission
     *
     * Sets the section ID, previous hashes, and signs the transaction.
     *
     * @param transaction The transaction to prepare
     * @param signer The actor who will sign the transaction
     * @return std::expected<Transaction, TransactionError> The prepared transaction or an error
     */
    std::expected<Transaction, TransactionError> prepare_transaction(const Transaction       &transaction,
                                                                     const Actor<KeyPrivate> &signer);

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
    std::expected<void, bool> network_transaction(const Transaction &transaction, const Responder &responder);

    /**
     * @brief Process a transaction validation result from the network
     *
     * Updates the local state based on transaction validation results.
     *
     * @param hash The hash of the transaction
     * @param result The validation result
     */
    void network_transaction_result(const std::string hash, TransactionProveError result);

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
    Balances calculate_actors_balance(const std::vector<ActorId> &actor_ids);

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
     */
    void update_range();

    /**
     * @brief Search for a transaction by its hash
     *
     * @param hash The hash to search for
     * @param deep The maximum number of sections to search back (default: 100)
     * @return std::optional<Transaction> The transaction if found, or nullopt
     */
    std::optional<Transaction> search_transaction(const std::string &hash, int deep = 100) const;

    /**
     * @brief Read a section from storage
     *
     * @param section_id The ID of the section to read
     * @return std::optional<Section> The section if found, or nullopt
     */
    std::optional<Section> read_section(const BigNumber &section_id) const;

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
    void network_request_sections(const BigNumber &from, const BigNumber &to, const Responder &responder);

    /**
     * @brief Process a sections response from the network
     *
     * @param compressed The compressed sections data
     * @param responder The responder that sent the data
     */
    void network_request_sections_response(const std::string &compressed, const Responder &responder);

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
    void process_cached_transactions();

private:
    ExtraChainNode                              *node;                 // Parent node reference
    TransactionCache                             transaction_cache_;   // Transaction cache for fast lookups
    std::unordered_map<std::string, Transaction> sended_transactions_; // Transactions sent but not yet confirmed
    DagCache                                     cache_;               // Balance cache for fast calculations

    BigNumber current_section_     = BigNumber(-1);      // Current (latest) section ID
    BigNumber first_saved_section_ = BigNumber(-1);      // First section ID saved in the chain
    DagMode   mode_                = DagMode::Full;      // Current operation mode
    DagStatus status_              = DagStatus::Started; // Current operational status

    DagSyncStatus                                sync_status_  = DagSyncStatus::None; // Current sync status
    DagSyncStatus                                check_status_ = DagSyncStatus::None; // Current check status
    BigNumber                                    sync_last_index;                     // Last section index to sync
    int                                          requests_count = 0; // Number of outstanding requests
    std::unordered_map<std::string, DagLastInfo> last_info_;         // Last chain info from peers
    QTimer                                      *timer_sync;         // Timer for sync operations
    std::uint64_t                                timestamp_bigger_sync_start_ = 0;

    rustex::mutex<std::set<Transaction>> cached_txs_; // Transactions cached during synchronization

    //
    void add_to_cached_tx(const Transaction &transaction);

    /**
     * @brief Request sections from the network
     *
     * @param from Starting section ID
     * @param to Ending section ID
     * @param responder Responder to send the request to
     */
    void request_sections(const BigNumber &from, const BigNumber &to, const Responder &responder);

    /**
     * @brief Send a sync request to the network
     *
     * Determines what to sync based on peer information and sends appropriate requests.
     */
    void send_sync_request();

    /**
     * @brief Write a section to storage
     *
     * @param section The section to write
     * @return std::optional<bool> Success or failure
     */
    std::optional<bool> write_section(const Section &section);

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
    std::optional<std::pair<BigNumber, BigNumber>> save_transactions(const std::vector<Transaction> &transactions);

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
    TransactionProveError prove_transaction(const Transaction &tx, const std::set<Transaction> &transactions);

    void clear_dag();

    friend class ExtraChainNode;
};
