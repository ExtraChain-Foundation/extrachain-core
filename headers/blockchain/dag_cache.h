#pragma once

#include <memory>
#include <map>
#include <unordered_map>
#include <set>
#include <vector>
#include <optional>

#include "utils/bignumber.h"
#include "blockchain/transaction.h"

class ExtraChainNode;
class DbConnector;
class Section;

// Cache configuration constants
constexpr int CACHE_LAG_SECTIONS = 15; // Safe lag between current section and persistent cache

/**
 * @brief Pair of actor and token for balance tracking
 */
struct ActorPair {
    ActorId actor_id;
    TokenId token_id;

    // Comparison operator for std::map
    bool operator<(const ActorPair& other) const {
        if (actor_id != other.actor_id)
            return actor_id < other.actor_id;
        return token_id < other.token_id;
    }
};
BOOST_DESCRIBE_STRUCT(ActorPair, (), (actor_id, token_id))

/**
 * @brief DagCache - Manages caching of actor balances for blockchain
 *
 * This class handles in-memory and database caching of actor balances
 * to accelerate balance calculations and support light mode.
 */
class DagCache {
public:
    /**
     * @brief Construct a new DagCache object
     *
     * @param node The ExtraChainNode reference
     */
    DagCache(ExtraChainNode* node);

    /**
     * @brief Destroy the DagCache object
     */
    ~DagCache();

    /**
     * @brief Get the current section ID of the cache
     *
     * @return BigNumber The section ID
     */
    BigNumber section() const {
        return section_;
    }

    /**
     * @brief Set the current section ID of the cache
     *
     * @param section_id The new section ID
     */
    void set_section(const BigNumber& section_id) {
        section_ = section_id;
    }

    /**
     * @brief Get the balance for a specific actor-token pair
     *
     * @param actor_id The actor ID
     * @param token_id The token ID
     * @return BigNumberFloat The balance
     */
    BigNumberFloat get_balance(const ActorId& actor_id, const TokenId& token_id) const;

    /**
     * @brief Set the balance for a specific actor-token pair
     *
     * @param actor_id The actor ID
     * @param token_id The token ID
     * @param balance The balance to set
     */
    void set_balance(const ActorId& actor_id, const TokenId& token_id, const BigNumberFloat& balance);

    /**
     * @brief Update cache for a single transaction
     *
     * @param transaction The transaction to process
     */
    void update_for_transaction(const Transaction& transaction);

    /**
     * @brief Update cache to specified section
     *
     * @param section_id The target section ID
     * @param current_section The current section of the blockchain
     * @param first_saved_section The first saved section of the blockchain
     * @return true If update was successful
     * @return false If update failed
     */
    bool update_to_section(const BigNumber& section_id,
                           const BigNumber& current_section,
                           const BigNumber& first_saved_section);

    /**
     * @brief Calculate balances for actors using cache
     *
     * @param actor_ids Vector of actor IDs
     * @param token_id Token ID
     * @param current_section Current section of the blockchain
     * @param first_saved_section First saved section of the blockchain
     * @param read_section_callback Function to read a section
     * @return std::unordered_map<ActorId, BigNumberFloat> Map of actor balances
     */
    std::unordered_map<ActorId, BigNumberFloat> calculate_balances(
        const std::vector<ActorId>&                             actor_ids,
        const TokenId&                                          token_id,
        const BigNumber&                                        current_section,
        const BigNumber&                                        first_saved_section,
        std::function<std::optional<Section>(const BigNumber&)> read_section_callback);

    /**
     * @brief Flush cache to database
     *
     * @param current_section Current section of the blockchain
     * @return true If flush was successful
     * @return false If flush failed
     */
    bool flush_to_db(const BigNumber& current_section);

    /**
     * @brief Load cache from database
     *
     * @return true If load was successful
     * @return false If load failed
     */
    bool load_from_db();

    /**
     * @brief Request cache from network
     *
     * @param section_id Section ID to request
     */
    void request_from_network(const BigNumber& section_id);

    /**
     * @brief Calculate the section ID for cache boundaries
     *
     * @param id Section ID to align
     * @return BigNumber Aligned cache section ID
     */
    BigNumber calculate_cache_id(const BigNumber& id) const;

    /**
     * @brief Check if database cache needs updating and update it
     *
     * @param current_section Current section of the blockchain
     * @return true If cache was updated
     * @return false If no update was needed or failed
     */
    bool check_and_update_db(const BigNumber& current_section);

    /**
     * @brief Clear all cached balances
     */
    void clear_balances() {
        balances_.clear();
    }

    /**
     * @brief Initialize database connection
     *
     * @return true If initialization was successful
     * @return false If initialization failed
     */
    bool init_db();

    void reset_db();

private:
    ExtraChainNode*                     node_;                    // Node reference
    BigNumber                           section_ = BigNumber(-1); // Current section ID of cache
    std::map<ActorPair, BigNumberFloat> balances_;                // In-memory balance cache
    std::unique_ptr<DbConnector>        db_;                      // Database connection
    bool                                db_initialized_ = false;  // Whether DB is initialized

    /**
     * @brief Process a transaction for balance updates
     *
     * @param tx Transaction to process
     * @param actor_ids Vector of actor IDs to update
     * @param token_id Token ID
     * @param balances Map of balances to update
     */
    void process_transaction_for_balance(const Transaction&                           tx,
                                         const std::vector<ActorId>&                  actor_ids,
                                         const TokenId&                               token_id,
                                         std::unordered_map<ActorId, BigNumberFloat>& balances);

    /**
     * @brief Calculate balances from scratch without using cache
     *
     * @param actor_ids Vector of actor IDs
     * @param token_id Token ID
     * @param start_section Section to start calculation from
     * @param first_saved_section First saved section of the blockchain
     * @param read_section_callback Function to read a section
     * @return std::unordered_map<ActorId, BigNumberFloat> Map of actor balances
     */
    std::unordered_map<ActorId, BigNumberFloat> calculate_balances_internal(
        const std::vector<ActorId>&                             actor_ids,
        const TokenId&                                          token_id,
        const BigNumber&                                        start_section,
        const BigNumber&                                        first_saved_section,
        std::function<std::optional<Section>(const BigNumber&)> read_section_callback);
};
