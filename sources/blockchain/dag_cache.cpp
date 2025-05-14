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
#include "blockchain/dag_cache.h"
#include "blockchain/dag.h"
#include "managers/extrachain_node.h"
#include "utils/db_connector.h"

DagCache::DagCache(ExtraChainNode* node)
    : node_(node) {
}

DagCache::~DagCache() {
    // Ensure DB is closed
    if (db_ && db_->is_open()) {
        db_->close();
    }
}

std::pair<BigNumber, Balances> DagCache::read_cached_balances() {
    Balances balances;

    // Check if database is initialized
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for read_cached_balances");
        return { BigNumber(-1), balances }; // TODO: expected
    }

    const auto rows          = db_->select("SELECT * FROM balance_cache");
    auto       cache_section = cached_section_;

    for (const auto& row : rows) {
        auto actor_id = ActorId::create(row.at("actor_id"));
        auto token_id = ActorId::create(row.at("token_id"));
        auto balance  = BigNumberFloat::create(row.at("balance"));

        if (!actor_id.has_value() || !token_id.has_value() || !balance.has_value()) {
            continue;
        }

        balances[{ actor_id.value(), token_id.value() }] = balance.value();
    }

    return { cache_section, balances };
}

void DagCache::write_cached_balances(const Balances& balances, const std::optional<BigNumber>& section_id) {
    // Check if database is initialized
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for write_cached_balances");
        return;
    }

    // Start a transaction for efficiency
    db_->query("BEGIN TRANSACTION");

    // Write each balance to the database
    for (const auto& [key, balance] : balances) {
        const ActorId& actor_id = key.first;
        const TokenId& token_id = key.second;

        // Skip zero balances to save space (consistent with write_cached_balance)
        if (balance == BigNumberFloat(0)) {
            DbRow where = { { "actor_id", actor_id.to_string() }, { "token_id", token_id.to_string() } };
            db_->delete_row("balance_cache", where);
        } else {
            DbRow data = { { "actor_id", actor_id.to_string() },
                           { "token_id", token_id.to_string() },
                           { "balance", balance.to_string() } };
            db_->replace("balance_cache", data);
        }
    }

    // Commit transaction
    db_->query("COMMIT");

    // Update cached section if provided
    if (section_id.has_value()) {
        set_section(section_id.value());
        eLog("[DagCache] Updated cache section to {}", section_id.value());
    }

    eLog("[DagCache] Wrote {} balances to cache", balances.size());
}

BigNumberFloat DagCache::read_cached_balance(const ActorId& actor_id, const TokenId& token_id) {
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

void DagCache::write_cached_balance(const ActorId&        actor_id,
                                    const TokenId&        token_id,
                                    const BigNumberFloat& balance) {
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for write_cached_balance");
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

    // The cached_section_ may be earlier than genesis_section due to lag
    // We need to use the actual cached_section_ for balance calculations
    bool      use_cache = false;
    BigNumber balance_start_section;

    // Check if we have a valid cache that we can use
    if (cached_section_ != BigNumber(-1) && init_db()) {
        // We have some cache, which may be at an earlier point than the genesis_section
        use_cache             = true;
        balance_start_section = cached_section_ + 1;

        // Get cached balances from DB
        for (const auto& actor_id : actor_ids) {
            balances[actor_id] = read_cached_balance(actor_id, token_id);
            // eLog("[DagCache] Found cached balance for actor {}: {}", actor_id, balances[actor_id]);
        }
    } else {
        // No usable cache found
        if (node_->dag()->mode() == DagMode::Light) {
            // Light mode requires cache from network if not available
            // eLog("[DagCache] Light mode missing cache - requesting from network");
            // Request cache from network here (future implementation)
            return balances; // Return empty balances, will retry when cache is available
        } else {
            // Full mode can recalculate cache if needed
            // We'll calculate up to the safe section (with lag)
            BigNumber safe_section_with_lag = current_section;
            if (current_section > BigNumber(CACHE_LAG_SECTIONS)) {
                safe_section_with_lag = current_section - CACHE_LAG_SECTIONS;
            }

            BigNumber safe_genesis_section = calculate_genesis_section(safe_section_with_lag);

            eLog("[DagCache] Recalculating cache from scratch to safe section {}", safe_genesis_section);

            // Calculate the genesis section balances
            auto success = update_to_genesis_section(safe_genesis_section,
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

            // If cache update failed, we'll calculate from the beginning
            balance_start_section = first_saved_section;
        }
    }

    // If we get here with use_cache == false and not in light mode,
    // we need to calculate from first_saved_section to current_section
    if (!use_cache && balance_start_section == 0 && balance_start_section != BigNumber(-1)) {
        // No valid starting section, use first_saved_section
        balance_start_section = first_saved_section;
    }

    // Process transactions after the balance_start_section up to current_section
    // eLog("[DagCache] Processing transactions from section {} to {}", balance_start_section, current_section);

    for (BigNumber i = balance_start_section; i <= current_section; i++) {
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
    // Calculate safe section ID based on lag
    // We only want to cache sections that are at least CACHE_LAG_SECTIONS behind the current section
    if (current_section < BigNumber(CACHE_LAG_SECTIONS)) {
        // If we don't have enough sections yet, don't cache anything
        // eLog("[DagCache] Not enough sections for caching: current={}, required lag={}",
        //      current_section,
        //      CACHE_LAG_SECTIONS);
        return false;
    }

    // First, calculate the section with lag
    BigNumber safe_section_with_lag = current_section - CACHE_LAG_SECTIONS;

    // Then, find the nearest genesis section (multiple of CONSTRUCT_GENESIS_EVERY_BLOCKS)
    BigNumber safe_genesis_section = calculate_genesis_section(safe_section_with_lag);

    // eLog("[DagCache] Checking cache update: current={}, with_lag={}, cached={}, safe_genesis={}",
    //      current_section,
    //      safe_section_with_lag,
    //      cached_section_,
    //      safe_genesis_section);

    // Don't update if already at or ahead of safe section
    if (cached_section_ != BigNumber(-1) && safe_genesis_section <= cached_section_) {
        // eLog("[DagCache] No cache update needed: safe_genesis_section <= cached_section_");
        return false;
    }

    // Don't update if would be moving backwards
    if (cached_section_ > safe_genesis_section) {
        eLog("[DagCache] Invalid cache update: cached_section_ > safe_genesis_section");
        return false;
    }

    // Don't update if the distance between the current cache section and the new safe section
    // is less than CACHE_LAG_SECTIONS (to prevent frequent updates)
    if (cached_section_ != BigNumber(-1)
        && (safe_genesis_section - cached_section_) < BigNumber(CACHE_LAG_SECTIONS)) {
        // eLog("[DagCache] Skipping cache update: not enough new sections since last update");
        return false;
    }

    eLog("[DagCache] Cache update needed: current_section={}, cached_section={}, safe_genesis={}",
         current_section,
         cached_section_,
         safe_genesis_section);

    // Use read_section callback from DAG
    auto read_section_callback = [this](const BigNumber& section_id) -> std::optional<Section> {
        return node_->dag()->read_section(section_id);
    };

    // Update cache to safe section (genesis + lag)
    bool result = update_to_genesis_section(safe_genesis_section,
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
                write_cached_balance(actor_id, token_id, it->second);
            }
        }
    }

    // Commit transaction
    db_->query("COMMIT");

    // Update cached section
    cached_section_ = genesis_section;

    eLog("[DagCache] Cache updated to section {}", cached_section_);
    node_->dag()->update_range();

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

    QDir().mkdir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER));
    QDir().mkdir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_CACHE_FOLDER));

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
