#pragma once

#include <memory>
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
 * @brief DagCache - Manages caching of actor balances for blockchain
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
        return cached_section_;
    }

    /**
     * @brief Set the current section ID of the cache
     *
     * @param section_id The new section ID
     */
    void set_section(const BigNumber& section_id) {
        cached_section_ = section_id;
    }

    /**
     * @brief Get the balance for a specific actor-token pair from cache
     *
     * @param actor_id The actor ID
     * @param token_id The token ID
     * @param section_id The section ID to get balance for
     * @return BigNumberFloat The balance
     */
    BigNumberFloat get_cached_balance(const ActorId&   actor_id,
                                      const TokenId&   token_id,
                                      const BigNumber& section_id);

    /**
     * @brief Set the balance for a specific actor-token pair in cache
     *
     * @param actor_id The actor ID
     * @param token_id The token ID
     * @param balance The balance to set
     * @param section_id The section ID to set balance for
     */
    void set_cached_balance(const ActorId&        actor_id,
                            const TokenId&        token_id,
                            const BigNumberFloat& balance,
                            const BigNumber&      section_id);

    /**
     * @brief Update cache for a single transaction
     *
     * @param transaction The transaction to process
     */
    void update_for_transaction(const Transaction& transaction);

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
     * @brief Calculate the genesis section ID for caching
     *
     * @param section_id Current section ID
     * @return BigNumber Genesis section ID (multiple of GENESIS_SECTION_SIZE)
     */
    BigNumber calculate_genesis_section(const BigNumber& section_id) const;

    /**
     * @brief Check and update cache to latest safe section
     *
     * @param current_section Current section of the blockchain
     * @return true If cache was updated
     * @return false If no update was needed or failed
     */
    bool check_and_update_cache(const BigNumber& current_section);

    /**
     * @brief Update cache to a specific genesis section
     *
     * @param genesis_section The genesis section to update to
     * @param current_section Current section of the blockchain
     * @param first_saved_section First saved section in the blockchain
     * @param read_section_callback Function to read sections
     * @return true If update was successful
     * @return false If update failed
     */
    bool update_to_genesis_section(const BigNumber&                                        genesis_section,
                                   const BigNumber&                                        current_section,
                                   const BigNumber&                                        first_saved_section,
                                   std::function<std::optional<Section>(const BigNumber&)> read_section_callback);

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

private:
    // Helper struct for hashing actor-token pairs in unordered_map
    struct PairHash {
        template <class T1, class T2>
        std::size_t operator()(const std::pair<T1, T2>& p) const {
            auto h1 = std::hash<std::string> {}(p.first.to_string());
            auto h2 = std::hash<std::string> {}(p.second.to_string());
            return h1 ^ (h2 << 1);
        }
    };

    ExtraChainNode*              node_;                           // Node reference
    BigNumber                    cached_section_ = BigNumber(-1); // Current cached section ID (genesis point)
    std::unique_ptr<DbConnector> db_;                             // Database connection
    bool                         db_initialized_ = false;         // Whether DB is initialized

    /**
     * @brief Process transaction for balances
     *
     * @param transaction Transaction to process
     * @param actor_ids Set of actor IDs
     * @param balances Map of actor-token balances to update
     */
    void process_transaction(const Transaction&       transaction,
                             const std::set<ActorId>& actor_ids,
                             std::unordered_map<std::pair<ActorId, TokenId>, BigNumberFloat, PairHash>& balances);

    /**
     * @brief Load cache state from database
     *
     * @return true If loading was successful
     * @return false If loading failed
     */
    bool load_from_db();
};

// Hash function for std::pair to use in unordered_map
struct PairHash {
    template <class T1, class T2>
    std::size_t operator()(const std::pair<T1, T2>& p) const {
        auto h1 = std::hash<std::string> {}(p.first.to_string());
        auto h2 = std::hash<std::string> {}(p.second.to_string());
        return h1 ^ (h2 << 1);
    }
};
