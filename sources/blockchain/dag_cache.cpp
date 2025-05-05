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

BigNumberFloat DagCache::get_balance(const ActorId& actor_id, const TokenId& token_id) {
    if (!init_db()) {
        return BigNumberFloat(0);
    }

    DbRow binds = { { "section_id", section_.to_string() },
                    { "actor_id", actor_id.to_string() },
                    { "token_id", token_id.to_string() } };

    auto rows = db_->select(
        "SELECT balance FROM balance_cache WHERE section_id = @section_id AND actor_id = @actor_id AND token_id = "
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

void DagCache::set_balance(const ActorId& actor_id, const TokenId& token_id, const BigNumberFloat& balance) {
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize DB for set_balance");
        return;
    }

    DbRow data = { { "section_id", section_.to_string() },
                   { "actor_id", actor_id.to_string() },
                   { "token_id", token_id.to_string() },
                   { "balance", balance.to_string() } };

    if (balance == BigNumberFloat(0)) {
        // Remove zero balances to save space
        DbRow where = { { "section_id", section_.to_string() },
                        { "actor_id", actor_id.to_string() },
                        { "token_id", token_id.to_string() } };
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

    // Get actors affected by this transaction
    std::vector<ActorId> actors;
    if (!transaction.sender().is_zero()) {
        actors.push_back(transaction.sender());
    }
    if (!transaction.receiver().is_zero() && transaction.receiver() != transaction.sender()) {
        actors.push_back(transaction.receiver());
    }

    if (actors.empty()) {
        return;
    }

    // Get tokens affected by this transaction
    std::vector<TokenId> tokens = { transaction.token() };

    // Also handle from_token in conversion transactions
    if (transaction.type() == TransactionType::Conversion && transaction.data().has_value()) {
        auto from_token = TokenId::create(transaction.data().value());
        if (from_token.has_value() && from_token.value() != transaction.token()) {
            tokens.push_back(from_token.value());
        }
    }

    // Initialize DB transaction
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize DB for update_for_transaction");
        return;
    }

    db_->query("BEGIN TRANSACTION");

    // Process the transaction for each actor-token pair
    for (const auto& actor_id : actors) {
        for (const auto& token_id : tokens) {
            // Get current balance
            BigNumberFloat balance = get_balance(actor_id, token_id);

            // Update balance based on transaction type
            if (transaction.type() == TransactionType::Reward && transaction.sender() == actor_id
                && transaction.token() == token_id) {
                balance += transaction.amount();
            } else if (transaction.type() == TransactionType::InitContract && transaction.sender() == actor_id
                       && transaction.token() == token_id) {
                balance += transaction.amount();
            } else if (transaction.type() == TransactionType::Conversion && transaction.sender() == actor_id) {
                if (transaction.data().has_value()) {
                    auto from_token = TokenId::create(transaction.data().value());
                    if (from_token.has_value()) {
                        if (from_token.value() == token_id) {
                            balance -= transaction.amount();
                        }
                        if (transaction.token() == token_id) {
                            balance += transaction.amount();
                        }
                    }
                }
            } else {
                if (transaction.receiver() == actor_id && transaction.token() == token_id) {
                    balance += transaction.amount();
                }
                if (transaction.sender() == actor_id && transaction.token() == token_id) {
                    balance -= transaction.amount();
                }
            }

            // Update the cache with the new balance
            set_balance(actor_id, token_id, balance);
        }
    }

    // Commit transaction
    db_->query("COMMIT");

    eLog("[DagCache] Updated balances for transaction: {}", transaction.hash());
}

bool DagCache::update_to_section(const BigNumber& section_id,
                                 const BigNumber& current_section,
                                 const BigNumber& first_saved_section) {
    // If trying to update to same section, nothing to do
    if (section_ == section_id) {
        return true;
    }

    eLog("[DagCache] Updating cache to section: {}", section_id);

    // Set new section ID
    section_ = section_id;

    // Find unique actors and tokens from transactions
    std::set<ActorId> unique_actors;
    std::set<TokenId> unique_tokens;

    auto read_section_callback = [this](const BigNumber& section_id) -> std::optional<Section> {
        return node_->dag()->read_section(section_id);
    };

    // Scan from section_id down to first saved section to collect actors and tokens
    for (BigNumber i = section_id; i >= first_saved_section; i--) {
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
        eLog("[DagCache] Failed to initialize DB for update_to_section");
        return false;
    }

    // Clear existing entries for this section
    db_->query("DELETE FROM balance_cache WHERE section_id = '" + section_id.to_string() + "'");

    // Start a transaction for efficiency
    db_->query("BEGIN TRANSACTION");

    // Calculate balances for actor-token pairs with transactions
    for (const auto& actor_id : unique_actors) {
        for (const auto& token_id : unique_tokens) {
            std::vector<ActorId> actor_vec   = { actor_id };
            auto                 balance_map = calculate_balances_internal(actor_vec,
                                                           token_id,
                                                           section_id,
                                                           first_saved_section,
                                                           read_section_callback);

            if (!balance_map.empty() && balance_map[actor_id] != BigNumberFloat(0)) {
                // Store non-zero balance
                DbRow data = { { "section_id", section_id.to_string() },
                               { "actor_id", actor_id.to_string() },
                               { "token_id", token_id.to_string() },
                               { "balance", balance_map[actor_id].to_string() } };

                db_->replace("balance_cache", data);
            }
        }
    }

    // Commit transaction
    db_->query("COMMIT");

    // Update SectionRange
    node_->dag()->update_range();

    eLog("[DagCache] Cache updated to section {}", section_id);

    return true;
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

    eLog("[DagCache] Calculate balances for {} actors and token {}", actor_ids.size(), token_id);

    if (current_section == BigNumber(-1)) {
        return balances;
    }

    // Find the latest cache section before current
    BigNumber cache_section = calculate_cache_id(current_section);

    // Check if we have this cache in the DB
    bool used_cache = false;

    if (section_ == cache_section && init_db()) {
        used_cache = true;

        // Get balances from DB
        for (const auto& actor_id : actor_ids) {
            // TODO!
            // balances[actor_id] = get_balance(actor_id, token_id);
            balances[actor_id] = BigNumberFloat(0);
        }
    }

    // If couldn't use cache, handle based on mode
    if (!used_cache) {
        if (node_->dag()->mode() == DagMode::Light) {
            // Light mode requires cache
            // !!! Cache should be requested from network here
            eLog("[DagCache] Light mode missing cache for section {}", cache_section);
            request_from_network(cache_section);

            // Return empty result, will retry when cache is available
            return balances;
        } else {
            // TODO! Redone this, because loop
            // Full mode can calculate from scratch
            eLog("[DagCache] Recalculating cache from scratch for section {}", cache_section);
            update_to_section(cache_section, current_section, first_saved_section);

            // Try again with the newly calculated cache
            return calculate_balances(actor_ids,
                                      token_id,
                                      current_section,
                                      first_saved_section,
                                      read_section_callback);
        }
    }

    // Process transactions after the cache section
    for (BigNumber i = current_section; i > cache_section; i--) {
        auto section = read_section_callback(i);
        if (!section.has_value() || section->transactions.empty() || section->id < 0) {
            continue;
        }

        // Process each transaction in the section
        for (auto& tx : section->transactions) {
            process_transaction_for_balance(tx, actor_ids, token_id, balances);
        }
    }

    return balances;
}

bool DagCache::flush_to_db() {
    // Since we now write directly to the DB, this is mostly a no-op
    // Just make sure DB is initialized and update the range file

    if (!init_db()) {
        eLog("[DagCache] Failed to initialize cache database");
        return false;
    }

    eLog("[DagCache] Cache flushed to database for section {}", section_);

    // Update range file to reflect cache section
    node_->dag()->update_range();

    // !!! Cache is sent to network? Or only for request?

    return true;
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

    section_ = max_section.value();
    eLog("[DagCache] Loaded last cached section: {}", section_);

    return true;
}

void DagCache::request_from_network(const BigNumber& section_id) {
    // !!! Cache should be requested
    eLog("[DagCache] Requesting cache for section {} from network", section_id);
}

BigNumber DagCache::calculate_cache_id(const BigNumber& id) const {
    return id / Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS
           * Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
}

bool DagCache::check_and_update_db(const BigNumber& current_section) {
    // Calculate safe section ID (with lag)
    auto safe_cache_id = calculate_cache_id(current_section - CACHE_LAG_SECTIONS);

    eLog("[DagCache] Checking cache update: current_section={}, section_={}, safe_cache_id={}",
         current_section,
         section_,
         safe_cache_id);

    // Don't update if safe cache ID is behind or equal to current cache
    if (section_ != BigNumber(-1) && safe_cache_id <= section_) {
        eLog("[DagCache] Skipping update: safe_cache_id <= section_");
        return false;
    }

    // Don't update if we'd be moving backwards
    if (section_ > safe_cache_id) {
        eLog("[DagCache] Skipping update: section_ > safe_cache_id");
        return false;
    }

    eLog("[DagCache] Safe cache update needed: current={}, safe={}", section_, safe_cache_id);

    BigNumber old_section = section_;
    section_              = safe_cache_id;

    node_->dag()->update_range();

    // TODO! full cache update?
    bool result = update_to_section(safe_cache_id, current_section, node_->dag()->first_saved_section());

    if (!result) {
        section_ = old_section;
        node_->dag()->update_range();
        return false;
    }

    return true;
}

bool DagCache::force_update_db(const BigNumber& current_section) {
    eLog("[DagCache] FORCE updating cache to DB");
    auto safe_cache_id = calculate_cache_id(current_section - CACHE_LAG_SECTIONS);

    bool result = update_to_section(safe_cache_id, current_section, node_->dag()->first_saved_section());

    if (result) {
        eLog("[DagCache] FORCE update successful: section={}", section_);
        return true;
    } else {
        eLog("[DagCache] FORCE update failed");
        return false;
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

    std::string db_path = BlockchainConst::BLOCKCHAIN_FOLDER + "/balance_cache.db";
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

void DagCache::process_transaction_for_balance(const Transaction&                           tx,
                                               const std::vector<ActorId>&                  actor_ids,
                                               const TokenId&                               token_id,
                                               std::unordered_map<ActorId, BigNumberFloat>& balances) {

    for (const auto& actor_id : actor_ids) {
        if (tx.type() == TransactionType::Reward && tx.sender() == actor_id && tx.token() == token_id) {
            balances[actor_id] += tx.amount();
            continue;
        }

        if (tx.type() == TransactionType::InitContract && tx.sender() == actor_id && tx.token() == token_id) {
            balances[actor_id] += tx.amount();
            continue;
        }

        if (tx.type() == TransactionType::Conversion && tx.sender() == actor_id) {
            if (!tx.data().has_value()) {
                continue;
            }
            auto from_token = ActorId::create(tx.data().value());
            if (!from_token.has_value()) {
                continue;
            }

            if (from_token.value() == token_id) {
                balances[actor_id] -= tx.amount();
            }

            if (tx.token() == token_id) {
                balances[actor_id] += tx.amount();
            }
            continue;
        }

        if (tx.receiver() == actor_id && tx.token() == token_id) {
            balances[actor_id] += tx.amount();
        }

        if (tx.sender() == actor_id && tx.token() == token_id) {
            balances[actor_id] -= tx.amount();
        }
    }
}

std::unordered_map<ActorId, BigNumberFloat> DagCache::calculate_balances_internal(
    const std::vector<ActorId>&                             actor_ids,
    const TokenId&                                          token_id,
    const BigNumber&                                        start_section,
    const BigNumber&                                        first_saved_section,
    std::function<std::optional<Section>(const BigNumber&)> read_section_callback) {

    std::unordered_map<ActorId, BigNumberFloat> balances;

    for (const auto& actor_id : actor_ids) {
        balances[actor_id] = BigNumberFloat(0);
    }

    // Start from the requested section or from current if -1
    BigNumber begin_section = (start_section == BigNumber(-1)) ? section_ : start_section;

    for (BigNumber i = begin_section; i >= first_saved_section; i--) {
        auto section = read_section_callback(i);

        if (!section.has_value()) {
            continue;
        }

        if (section.has_value() && (section->transactions.empty() || section->id < 0)) {
            continue;
        }

        // Process each transaction in the section
        for (auto& tx : section->transactions) {
            process_transaction_for_balance(tx, actor_ids, token_id, balances);
        }
    }

    return balances;
}
