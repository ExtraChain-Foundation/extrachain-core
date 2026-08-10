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
#include <map>
#include <set>
#include <vector>
#include <optional>

#include "utils/bignumber.h"
#include "chain/transaction.h"
#include "contracts/contract_types.h"
#include "utils/exc_utils.h"

class ExtraChainNode;
class Dag;
struct Section;
class DbConnector;

// Cache configuration constants
constexpr int CACHE_LAG_SECTIONS = 15; // Safe lag between current section and persistent cache

using Balances = std::map<std::pair<ActorId, TokenId>, BigNumberFloat>;

struct CacheResult {
    bool      result;
    SectionId from;
    SectionId to;
};
BOOST_DESCRIBE_STRUCT(CacheResult, (), (result, from, to))

/**
 * @brief DagCache - Manages caching of actor balances for chain
 *
 * This class handles database caching of actor balances
 * to accelerate balance calculations and support light mode.
 */

class DagCache {
public:
    /**
     * @brief Construct a new DagCache object
     *
     * @param node The ExtraChainNode reference
     */
    DagCache(ExtraChainNode* node, Dag* dag);

    /**
     * @brief Destroy the DagCache object
     */
    ~DagCache();

    /**
     * @brief Get the current section id of the cache
     *
     * @return BigNumber The section id
     */
    BigNumber section() const;

    /**
     * @brief Set the current section id of the cache
     *
     * @param section_id The new section id
     */
    void set_section(const SectionId& section_id, Force force = Force::None);

    /**
     * @brief Read all cached balances from the database
     *
     * Retrieves all actor-token balances stored in the cache database.
     * Returns an empty map if database initialization fails.
     *
     * @return Balances Map of actor-token pairs to their balances
     */
    std::pair<SectionId, Balances>                read_cached_balances();
    std::optional<std::pair<SectionId, Balances>> read_cached_balances(
        const std::vector<std::pair<ActorId, TokenId>>& actor_token_pairs);

    std::optional<Balances> get_cached_balances_for_actors(const std::vector<ActorId>& actor_ids);

    /**
     * @brief Write all balances to the cache database
     *
     * Stores multiple actor-token balances in the database.
     * Zero balances are removed from the database to save space.
     * Uses a database transaction for efficiency when writing multiple entries.
     * If section_id is provided, updates the cached section to that value.
     *      * @param balances Map of actor-token pairs to their balances
     * @param section_id Optional section ID to update the cache section to
     */
    void write_cached_balances(const Balances&                 balances,
                               const std::optional<SectionId>& section_id = std::nullopt);

    /**
     * @brief Read the balance for a specific actor-token pair from cache
     *
     * @param actor_id The actor id
     * @param token_id The token id
     * @return BigNumberFloat The balance
     */
    BigNumberFloat read_cached_balance(const ActorId& actor_id, const TokenId& token_id);

    /**
     * @brief White the balance for a specific actor-token pair in cache
     *
     * @param actor_id The actor id
     * @param token_id The token id
     * @param balance The balance to set
     */
    void write_cached_balance(const ActorId& actor_id, const TokenId& token_id, const BigNumberFloat& balance);

    /**
     * @brief Calculate balances for actors using cache
     *
     * This method calculates balances for specified actors by:
     * 1. First checking if there's a valid cache available
     * 2. If cache exists, using it as the starting point
     * 3. If no cache exists:
     *    - In light mode: request from network and return empty balances
     *    - In full mode: recalculate cache up to a safe section (with lag)
     * 4. Processing all transactions from the cached section to the current section
     *      * The method respects the cache lag settings, ensuring consistency between
     * cache updates and balance calculations.
     *
     * @param actor_ids Vector of actor ids to calculate balances for
     * @param current_section Current section of the chain
     * @param first_saved_section First saved section of the chain
     * @param read_section_callback Function to read a section
     * @return std::map<ActorId, BigNumberFloat> Map of actor balances
     */
    Balances calculate_balances(const std::vector<ActorId>& actor_ids,
                                const SectionId&            current_section,
                                const SectionId&            first_saved_section,
                                std::optional<SectionId>    to_section = std::nullopt);

    // Derived live view. It avoids reading the mutable section tail for each
    // transaction from an active actor.
    void apply_live_transaction(const Transaction& transaction);
    void apply_live_transactions(const std::vector<Transaction>& transactions);
    void apply_transaction_delta(const Transaction& transaction, Balances& balances);
    void invalidate_live_balances();

    /**
     * @brief Calculate the genesis section id for caching
     *
     * @param section_id Current section id
     * @return BigNumber Genesis section id (multiple of CONSTRUCT_GENESIS_EVERY_BLOCKS)
     */
    BigNumber calculate_genesis_section(const SectionId& section_id) const;

    /**
     * @brief Check and update cache to latest safe section
     *
     *      * This method determines if the cache should be updated based on the current section
     * and the cache lag settings. The cache is only updated when:
     * 1. The current section is at least CACHE_LAG_SECTIONS ahead of the last cached section
     * 2. The safe section (current - lag) maps to a genesis section that's ahead of our cached section
     * 3. The distance between the current cached section and the new safe section is sufficient
     *      * This prevents frequent cache updates and ensures we only cache "mature" sections
     * that are unlikely to change.
     *
     * @param current_section Current section of the chain
     * @return true If cache was updated
     * @return false If no update was needed or update failed
     */
    CacheResult check_and_update_cache(const SectionId& current_section);

    void check_and_update_cache_thread(const SectionId& current_section);

    /**
     * @brief Update cache to a specific genesis section
     *
     * @param genesis_section The genesis section to update to
     * @param current_section Current section of the chain
     * @param first_saved_section First saved section in the chain
     * @param read_section_callback Function to read sections
     * @return true If update was successful
     * @return false If update failed
     */
    std::pair<bool, SectionId> update_to_genesis_section(
        const SectionId&                                        genesis_section,
        const SectionId&                                        current_section,
        const SectionId&                                        first_saved_section,
        std::function<std::optional<Section>(const SectionId&)> read_section_callback);

    /**
     * @brief Initialize database connection
     *
     * @return true If initialization was successful
     * @return false If initialization failed
     */
    bool init_db();

    /**
     * @brief Reset the database connection
     */
    void reset_db();

    std::set<ActorId> local_clear_less_balances(const SectionId& from           = SectionId(2),
                                                const Balances&  start_balances = Balances());

    // NOTE: per-actor transaction index (write_index/read_index/has_section/...) used
    // to live here on top of its own Index.db. It was slow on first pass and is being
    // rebuilt as ChainIndex (Phase 14) with proper batching/prepared statements and
    // compound indexes. Nothing here now — callers should use ChainIndex.

    void                                       index_contract_transaction(const Transaction& transaction);
    ExtraChain::Contracts::ContractCatalogPage list_contracts(
        const ExtraChain::Contracts::ContractCatalogFilter& filter = {});

private:
    ExtraChainNode*              node;                              // Node reference
    Dag*                         dag;                               // Dag reference
    SectionId                    cached_section_ = SectionId(-1);   // Current cached section id (genesis point)
    std::unique_ptr<DbConnector> cache_db_;                         // Database connection
    bool                         db_initialized_           = false; // Whether DB is initialized
    bool                         contract_catalog_scanned_ = false;
    std::mutex                   mutex_;
    std::mutex                   contract_catalog_mutex_;
    std::mutex                   live_balance_mutex_;
    Balances                     live_balances_;
    std::set<ActorId>            live_balance_actors_;
    SectionId                    live_balance_section_ = SectionId(-1);

public:
    /**
     * @brief Process transaction for balances
     *      * Updates balances for actors based on the given transaction.
     * Handles different transaction types:
     * - Reward: Increases sender's balance
     * - InitContract: Increases sender's balance
     * - Conversion: Transfers from one token to another
     * - Regular: Transfers from sender to receiver
     *      * @param transaction Transaction to process
     * @param balances Map of actor-token balances to update
     */
    void process_transaction(const Transaction& transaction, Balances& balances);

private:
    bool ensure_contract_catalog_schema();
    bool rebuild_contract_catalog();
    friend Dag;
};
