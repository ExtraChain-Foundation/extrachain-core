#include "blockchain/dag_cache.h"
#include "blockchain/dag.h"
#include "managers/extrachain_node.h"
#include "utils/db_connector.h"

DagCache::DagCache(ExtraChainNode* node)
    : node_(node) {
    // Load cache state from database
    load_from_db();
}

DagCache::~DagCache() {
    // Ensure DB is closed
    if (db_ && db_->is_open()) {
        db_->close();
    }
}

BigNumberFloat DagCache::get_cached_balance(const ActorId&   actor_id,
                                            const TokenId&   token_id,
                                            const BigNumber& section_id) {
    if (!init_db()) {
        return BigNumberFloat(0);
    }

    DbRow binds = { { "actor_id", actor_id.to_string() }, { "token_id", token_id.to_string() } };

    auto rows = db_->select(
        "SELECT balance FROM balance_cache WHERE actor_id = @actor_id AND token_id = "
        "@token_id",
        "balance_cache",
        binds);

    if (!rows.empty() && rows[0].contains("balance")) {
        auto balance = BigNumberFloat::create(rows[0]["balance"]);
        if (balance.has_value()) {
            return balance.value();
        }
    }

    return BigNumberFloat(0);
}

void DagCache::set_cached_balance(const ActorId&        actor_id,
                                  const TokenId&        token_id,
                                  const BigNumberFloat& balance,
                                  const BigNumber&      section_id) {
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize DB for set_cached_balance");
        return;
    }

    DbRow data = { { "actor_id", actor_id.to_string() },
                   { "token_id", token_id.to_string() },
                   { "balance", balance.to_string() } };

    if (balance == BigNumberFloat(0)) {
        // Remove zero balances to save space
        DbRow where = { { "actor_id", actor_id.to_string() }, { "token_id", token_id.to_string() } };
        db_->delete_row("balance_cache", where);
    } else {
        db_->replace("balance_cache", data);
    }
}

void DagCache::update_for_transaction(const Transaction& transaction) {
    // Skip if transaction doesn't affect balances
    if (transaction.type() == TransactionType::Unknown) {
        return;
    }

    // This function only updates in-memory balances for current section
    // Persistent caching is done in check_and_update_cache
    eLog("[DagCache] Transaction processed: {} in section {}", transaction.hash(), transaction.section());
}

BigNumber DagCache::calculate_genesis_section(const BigNumber& section_id) const {
    // Calculate the genesis section (multiple of Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS)
    return (section_id / Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS)
           * Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
}

std::unordered_map<ActorId, BigNumberFloat> DagCache::calculate_balances(
    const std::vector<ActorId>&                             actor_ids,
    const TokenId&                                          token_id,
    const BigNumber&                                        current_section,
    const BigNumber&                                        first_saved_section,
    std::function<std::optional<Section>(const BigNumber&)> read_section_callback) {

    std::unordered_map<ActorId, BigNumberFloat> balances;

    // Initialize balances to zero
    for (const auto& actor_id : actor_ids) {
        balances[actor_id] = BigNumberFloat(0);
    }

    eLog("[DagCache] Calculating balances for {} actors and token {}", actor_ids.size(), token_id);

    if (current_section == BigNumber(-1)) {
        return balances;
    }

    // Find the latest genesis section before current section
    BigNumber genesis_section = calculate_genesis_section(current_section);

    // Check if we have the cache in DB
    if (cached_section_ >= genesis_section && init_db()) {
        // Get cached balances from DB
        for (const auto& actor_id : actor_ids) {
            balances[actor_id] = get_cached_balance(actor_id, token_id, genesis_section);
            eLog("[DagCache] Found cached balance for actor {}: {}", actor_id, balances[actor_id]);
        }
    } else {
        if (node_->dag()->mode() == DagMode::Light) {
            // Light mode requires cache from network if not available
            eLog("[DagCache] Light mode missing cache for section {}", genesis_section);
            // Request cache from network here (future implementation)
            return balances; // Return empty balances, will retry when cache is available
        } else {
            // Full mode can recalculate cache if needed
            eLog("[DagCache] Recalculating cache from scratch for section {}", genesis_section);

            // Calculate the genesis section balances
            auto success = update_to_genesis_section(genesis_section,
                                                     current_section,
                                                     first_saved_section,
                                                     read_section_callback);

            if (success) {
                // Try again with the newly calculated cache
                return calculate_balances(actor_ids,
                                          token_id,
                                          current_section,
                                          first_saved_section,
                                          read_section_callback);
            }
        }
    }

    // Process transactions after the genesis section up to current section
    for (BigNumber i = genesis_section; i <= current_section; i++) {
        auto section = read_section_callback(i);
        if (!section.has_value() || section->transactions.empty() || section->id < 0) {
            continue;
        }

        // Process each transaction in the section
        for (const auto& tx : section->transactions) {
            if (tx.token() == token_id) {
                for (const auto& actor_id : actor_ids) {
                    // Process transaction effects on each actor's balance
                    if (tx.type() == TransactionType::Reward && tx.sender() == actor_id) {
                        balances[actor_id] += tx.amount();
                    } else if (tx.type() == TransactionType::InitContract && tx.sender() == actor_id) {
                        balances[actor_id] += tx.amount();
                    } else if (tx.type() == TransactionType::Conversion && tx.sender() == actor_id) {
                        if (tx.data().has_value()) {
                            auto from_token = TokenId::create(tx.data().value());
                            if (from_token.has_value() && from_token.value() == token_id) {
                                balances[actor_id] -= tx.amount();
                            }
                            if (tx.token() == token_id) {
                                balances[actor_id] += tx.amount();
                            }
                        }
                    } else {
                        if (tx.receiver() == actor_id && tx.token() == token_id) {
                            balances[actor_id] += tx.amount();
                        }
                        if (tx.sender() == actor_id && tx.token() == token_id) {
                            balances[actor_id] -= tx.amount();
                        }
                    }
                }
            }
        }
    }

    return balances;
}

bool DagCache::check_and_update_cache(const BigNumber& current_section) {
    // Calculate safe genesis section with lag
    BigNumber safe_section = calculate_genesis_section(current_section - CACHE_LAG_SECTIONS);

    eLog("[DagCache] Checking cache update: current={}, cached={}, safe_genesis={}",
         current_section,
         cached_section_,
         safe_section);

    // Don't update if already at or ahead of safe section
    if (cached_section_ != BigNumber(-1) && safe_section <= cached_section_) {
        eLog("[DagCache] No cache update needed: safe_section <= cached_section_");
        return false;
    }

    // Don't update if would be moving backwards
    if (cached_section_ > safe_section) {
        eLog("[DagCache] Invalid cache update: cached_section_ > safe_section");
        return false;
    }

    eLog("[DagCache] Cache update needed: current={}, safe={}", cached_section_, safe_section);

    // Use read_section callback from DAG
    auto read_section_callback = [this](const BigNumber& section_id) -> std::optional<Section> {
        return node_->dag()->read_section(section_id);
    };

    // Update cache to safe section (genesis + lag)
    bool result = update_to_genesis_section(safe_section,
                                            current_section,
                                            node_->dag()->first_saved_section(),
                                            read_section_callback);

    if (result) {
        // Update the section range to reflect new cache
        node_->dag()->update_range();
        return true;
    }

    return false;
}

bool DagCache::update_to_genesis_section(
    const BigNumber&                                        genesis_section,
    const BigNumber&                                        current_section,
    const BigNumber&                                        first_saved_section,
    std::function<std::optional<Section>(const BigNumber&)> read_section_callback) {

    // If trying to update to same section, nothing to do
    if (cached_section_ == genesis_section) {
        return true;
    }

    eLog("[DagCache] Updating cache to genesis section: {}", genesis_section);

    // Find all actors and tokens involved in transactions
    std::set<ActorId> unique_actors;
    std::set<TokenId> unique_tokens;

    // Scan from first_saved_section to genesis_section to collect actors and tokens
    for (BigNumber i = first_saved_section; i <= genesis_section; i++) {
        auto section = read_section_callback(i);
        if (!section.has_value() || section->transactions.empty()) {
            continue;
        }

        for (const auto& tx : section->transactions) {
            // Add sender and receiver to unique actors
            if (!tx.sender().is_zero()) {
                unique_actors.insert(tx.sender());
            }
            if (!tx.receiver().is_zero()) {
                unique_actors.insert(tx.receiver());
            }

            // Add token to unique tokens
            unique_tokens.insert(tx.token());

            // If Conversion transaction, also add from_token
            if (tx.type() == TransactionType::Conversion && tx.data().has_value()) {
                auto from_token = TokenId::create(tx.data().value());
                if (from_token.has_value()) {
                    unique_tokens.insert(from_token.value());
                }
            }
        }
    }

    eLog("[DagCache] Found {} unique actors and {} unique tokens for caching",
         unique_actors.size(),
         unique_tokens.size());

    // Initialize DB
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize DB for update_to_genesis_section");
        return false;
    }

    // Start a transaction for efficiency
    db_->query("BEGIN TRANSACTION");

    // Clear existing entries for this section
    // db_->query("DELETE FROM balance_cache WHERE section_id = '" + genesis_section.to_string() + "'");

    // Calculate and store balances for each actor-token pair
    std::unordered_map<std::pair<ActorId, TokenId>, BigNumberFloat, PairHash> balances;

    // Initialize balances map
    for (const auto& actor_id : unique_actors) {
        for (const auto& token_id : unique_tokens) {
            balances[{ actor_id, token_id }] = BigNumberFloat(0);
        }
    }

    // Process all transactions from first_saved_section to genesis_section
    for (BigNumber i = first_saved_section; i <= genesis_section; i++) {
        auto section = read_section_callback(i);
        if (!section.has_value() || section->transactions.empty()) {
            continue;
        }

        // Process each transaction
        for (const auto& tx : section->transactions) {
            process_transaction(tx, unique_actors, balances);
        }
    }

    // Store non-zero balances in the database
    for (const auto& actor_id : unique_actors) {
        for (const auto& token_id : unique_tokens) {
            auto it = balances.find({ actor_id, token_id });
            if (it != balances.end() && it->second != BigNumberFloat(0)) {
                set_cached_balance(actor_id, token_id, it->second, genesis_section);
            }
        }
    }

    // Commit transaction
    db_->query("COMMIT");

    // Update cached section
    cached_section_ = genesis_section;

    eLog("[DagCache] Cache updated to genesis section {}", genesis_section);

    return true;
}

void DagCache::process_transaction(
    const Transaction&                                                         tx,
    const std::set<ActorId>&                                                   actor_ids,
    std::unordered_map<std::pair<ActorId, TokenId>, BigNumberFloat, PairHash>& balances) {

    // Skip if transaction doesn't affect balances
    if (tx.type() == TransactionType::Unknown) {
        return;
    }

    for (const auto& actor_id : actor_ids) {
        // Reward transactions
        if (tx.type() == TransactionType::Reward && tx.sender() == actor_id /*&& !tx.token().is_zero()*/) {
            auto key = std::make_pair(actor_id, tx.token());
            if (balances.find(key) == balances.end()) {
                balances[key] = BigNumberFloat(0);
            }
            balances[key] += tx.amount();
        }
        // Contract initialization
        else if (tx.type() == TransactionType::InitContract && tx.sender() == actor_id && !tx.token().is_zero()) {
            auto key = std::make_pair(actor_id, tx.token());
            if (balances.find(key) == balances.end()) {
                balances[key] = BigNumberFloat(0);
            }
            balances[key] += tx.amount();
        }
        // Token conversion
        else if (tx.type() == TransactionType::Conversion && tx.sender() == actor_id) {
            if (tx.data().has_value()) {
                auto from_token = TokenId::create(tx.data().value());
                if (from_token.has_value()) {
                    // Deduct from source token
                    auto from_key = std::make_pair(actor_id, from_token.value());
                    if (balances.find(from_key) == balances.end()) {
                        balances[from_key] = BigNumberFloat(0);
                    }
                    balances[from_key] -= tx.amount();

                    // Add to destination token
                    auto to_key = std::make_pair(actor_id, tx.token());
                    if (balances.find(to_key) == balances.end()) {
                        balances[to_key] = BigNumberFloat(0);
                    }
                    balances[to_key] += tx.amount();
                }
            }
        }
        // Regular transactions
        else {
            // If actor is receiver, add funds
            if (tx.receiver() == actor_id && !tx.token().is_zero()) {
                auto key = std::make_pair(actor_id, tx.token());
                if (balances.find(key) == balances.end()) {
                    balances[key] = BigNumberFloat(0);
                }
                balances[key] += tx.amount();
            }

            // If actor is sender, deduct funds
            if (tx.sender() == actor_id && !tx.token().is_zero()) {
                auto key = std::make_pair(actor_id, tx.token());
                if (balances.find(key) == balances.end()) {
                    balances[key] = BigNumberFloat(0);
                }
                balances[key] -= tx.amount();
            }
        }
    }
}

bool DagCache::init_db() {
    if (db_initialized_) {
        return true;
    }

    if (db_ && db_->is_open()) {
        db_initialized_ = true;
        return true;
    }

    std::string db_path = BlockchainConst::BALANCE_CACHE;
    db_                 = std::make_unique<DbConnector>(db_path);

    if (!db_->open()) {
        eLog("[DagCache] Failed to open cache database");
        return false;
    }

    // Create table if it doesn't exist
    bool success = db_->query(Config::DataStorage::DagCacheCreate);

    if (!success) {
        eLog("[DagCache] Failed to create cache table");
        return false;
    }

    eLog("[DagCache] Cache database initialized");
    db_initialized_ = true;
    return true;
}

void DagCache::reset_db() {
    db_initialized_ = false;
    db_.reset();
}

bool DagCache::load_from_db() {
    if (!init_db()) {
        return false;
    }

    // Query the maximum section_id to find the latest cache
    auto rows = db_->select("SELECT MAX(section_id) as max_section FROM "
                            + std::string(Config::DataStorage::DagCacheTable));

    if (rows.empty() || !rows[0].contains("max_section") || rows[0]["max_section"].empty()) {
        eLog("[DagCache] No cached sections found in database");
        return false;
    }

    auto max_section = BigNumber::create(rows[0]["max_section"]);
    if (!max_section.has_value()) {
        eLog("[DagCache] Invalid section ID in database cache");
        return false;
    }

    cached_section_ = max_section.value();
    eLog("[DagCache] Loaded last cached section: {}", cached_section_);

    return true;
}
