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

BigNumber DagCache::section() const {
    return cached_section_;
}

void DagCache::set_section(const BigNumber& section_id) {
    cached_section_ = section_id;
}

std::pair<BigNumber, Balances> DagCache::read_cached_balances() {
    Balances balances;

    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for read_cached_balances");
        return { BigNumber(-1), balances }; // TODO: expected
    }

    const auto rows          = db_->select("SELECT * FROM balance_cache");
    auto       cache_section = cached_section_;

    for (const auto& row : rows) {
        auto actor_id = ActorId::create(row.at("actor_id"));
        auto token_id = TokenId::create(row.at("token_id"));
        auto balance  = BigNumberFloat::create(row.at("balance"));

        if (!actor_id.has_value() || !token_id.has_value() || !balance.has_value()) {
            continue;
        }

        balances[{ actor_id.value(), token_id.value() }] = balance.value();
    }

    return { cache_section, balances };
}

std::optional<std::pair<BigNumber, Balances>> DagCache::read_cached_balances(
    const std::vector<std::pair<ActorId, TokenId>>& actor_token_pairs) {
    Balances balances;

    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for read_cached_balance");
        return std::nullopt;
    }

    if (actor_token_pairs.empty()) {
        return std::pair { cached_section_, balances };
    }

    std::string              query = "SELECT * FROM balance_cache WHERE ";
    std::vector<std::string> conditions;
    DbRow                    binds;

    for (size_t i = 0; i < actor_token_pairs.size(); ++i) {
        const auto& pair        = actor_token_pairs[i];
        std::string actor_param = "actor_id_" + std::to_string(i);
        std::string token_param = "token_id_" + std::to_string(i);

        conditions.push_back("(actor_id = @" + actor_param + " AND token_id = @" + token_param + ")");
        binds[actor_param] = pair.first.to_string();
        binds[token_param] = pair.second.to_string();
    }

    query += boost::algorithm::join(conditions, " OR ");

    auto       cache_section = cached_section_;
    const auto rows          = db_->select(query, "balance_cache", binds);

    for (const auto& row : rows) {
        auto actor_id = ActorId::create(row.at("actor_id"));
        auto token_id = TokenId::create(row.at("token_id"));
        auto balance  = BigNumberFloat::create(row.at("balance"));
        if (!actor_id.has_value() || !token_id.has_value() || !balance.has_value()) {
            continue;
        }
        balances[{ actor_id.value(), token_id.value() }] = balance.value();
    }

    return std::pair { cached_section_, balances };
}

std::optional<Balances> DagCache::get_cached_balances_for_actors(const std::vector<ActorId>& actor_ids) {
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for get_cached_balances_for_actors");
        return std::nullopt;
    }

    if (actor_ids.empty()) {
        return Balances {};
    }

    std::string              query = "SELECT * FROM balance_cache WHERE actor_id IN (";
    std::vector<std::string> actor_ids_str;
    for (const auto& actor_id : actor_ids) {
        actor_ids_str.push_back("'" + actor_id.to_string() + "'");
    }
    query += boost::algorithm::join(actor_ids_str, ", ");
    query += ")";

    const auto rows = db_->select(query, "balance_cache");

    Balances balances;
    for (const auto& row : rows) {
        auto actor_id = ActorId::create(row.at("actor_id"));
        auto token_id = TokenId::create(row.at("token_id"));
        auto balance  = BigNumberFloat::create(row.at("balance"));
        if (!actor_id.has_value() || !token_id.has_value() || !balance.has_value()) {
            continue;
        }
        balances[{ actor_id.value(), token_id.value() }] = balance.value();
    }

    return balances;
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
        // if (balance == BigNumberFloat(0)) {
        //     DbRow where = { { "actor_id", actor_id.to_string() }, { "token_id", token_id.to_string() } };
        //     db_->delete_row("balance_cache", where);
        // } else {
        DbRow data = { { "actor_id", actor_id.to_string() },
                       { "token_id", token_id.to_string() },
                       { "balance", balance.to_string() } };
        db_->replace("balance_cache", data);
        // }
    }

    // Commit transaction
    db_->query("COMMIT");

    // Update cached section if provided
    if (section_id.has_value()) {
        set_section(section_id == BigNumber(0) ? BigNumber(-1) : section_id.value());
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

    // if (balance == BigNumberFloat(0)) {
    //     // Remove zero balances to save space
    //     DbRow where = { { "actor_id", actor_id.to_string() }, { "token_id", token_id.to_string() } };
    //     db_->delete_row("balance_cache", where);
    // } else {
    db_->replace("balance_cache", data);
    // }
}

BigNumber DagCache::calculate_genesis_section(const BigNumber& section_id) const {
    // Calculate the genesis section (multiple of Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS)
    return (section_id / Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS)
           * Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
}

Balances DagCache::calculate_balances(const std::vector<ActorId>& actor_ids,
                                      const BigNumber&            current_section,
                                      const BigNumber&            first_saved_section) {
    eLog("[DagCache] Calculating balances for {} actors", actor_ids.size());
    Balances balances;

    if (current_section == BigNumber(-1) || actor_ids.empty()) {
        return balances;
    }

    bool      use_cache = false;
    BigNumber balance_start_section;

    // Check if we have a valid cache that we can use
    if (cached_section_ != BigNumber(-1)) {
        // We have some cache, which may be at an earlier point than the genesis_section
        use_cache             = true;
        balance_start_section = cached_section_ + 1;

        // Получаем все кешированные балансы для запрошенных акторов
        auto cached_balances_opt = get_cached_balances_for_actors(actor_ids);
        if (cached_balances_opt.has_value()) {
            // Копируем все балансы из кеша
            balances = cached_balances_opt.value();
        }
    } else {
        // Если кеш недоступен, начинаем с первого сохраненного раздела
        balance_start_section = first_saved_section;
    }

    // Если начальный раздел не определен или 0, используем first_saved_section
    if (!use_cache && balance_start_section == 0 && balance_start_section != BigNumber(-1)) {
        balance_start_section = first_saved_section;
    }

    // Process transactions after the balance_start_section up to current_section
    for (BigNumber i = balance_start_section; i <= current_section; i++) {
        auto section = node_->dag()->read_section(i);
        if (!section.has_value() || section->transactions.empty() || section->id < 0) {
            continue;
        }

        // Process each transaction in the section
        for (const auto& tx : section->transactions) {
            // Проверяем, затрагивает ли транзакция наших акторов
            bool affects_our_actors = false;
            for (const auto& actor_id : actor_ids) {
                if (tx.sender() == actor_id || tx.receiver() == actor_id) {
                    affects_our_actors = true;
                    break;
                }
            }

            if (affects_our_actors) {
                process_transaction(tx, balances);
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

    BigNumber start_section;
    if (cached_section_ != BigNumber(-1)) {
        start_section = cached_section_ + 1;
    } else {
        start_section = first_saved_section;
    }

    // Создаем множество пар actor-token
    std::set<std::pair<ActorId, TokenId>> actor_token_set;

    // Scan from start_section to genesis_section to collect actor-token pairs
    for (BigNumber i = start_section; i <= genesis_section; i++) {
        auto section = read_section_callback(i);
        if (!section.has_value() || section->transactions.empty()) {
            continue;
        }
        for (const auto& tx : section->transactions) {
            // Add sender with token
            if (!tx.sender().is_zero()) {
                actor_token_set.insert({ tx.sender(), tx.token() });
            }

            // Add receiver with token
            if (!tx.receiver().is_zero()) {
                actor_token_set.insert({ tx.receiver(), tx.token() });
            }

            // If Conversion transaction, also add sender and receiver with from_token
            if (tx.type() == TransactionType::Conversion && tx.meta().has_value()) {
                auto from_token = TokenId::create(tx.meta().value());
                if (from_token.has_value()) {
                    if (!tx.sender().is_zero()) {
                        actor_token_set.insert({ tx.sender(), from_token.value() });
                    }
                    if (!tx.receiver().is_zero()) {
                        actor_token_set.insert({ tx.receiver(), from_token.value() });
                    }
                }
            }
        }
    }

    eLog("[DagCache] Found {} unique actor-token pairs for caching", actor_token_set.size());

    // Initialize DB
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize DB for update_to_genesis_section");
        return false;
    }

    // Start a transaction for efficiency
    db_->query("BEGIN TRANSACTION");

    // Преобразуем set в vector для функции read_cached_balances
    std::vector<std::pair<ActorId, TokenId>> actor_token_pairs(actor_token_set.begin(), actor_token_set.end());

    // Получаем все балансы одним запросом
    auto cached_balances_opt = read_cached_balances(actor_token_pairs);
    if (!cached_balances_opt.has_value()) {
        eLog("[DagCache] Failed to read cached balances");
        db_->query("ROLLBACK");
        return false;
    }

    // Используем полученные балансы напрямую как Balances
    Balances& balances = cached_balances_opt.value().second;

    // Process all transactions from start_section to genesis_section
    for (BigNumber i = start_section; i <= genesis_section; i++) {
        auto section = read_section_callback(i);
        if (!section.has_value() || section->transactions.empty()) {
            continue;
        }
        // Process each transaction
        for (const auto& tx : section->transactions) {
            process_transaction(tx, balances); // Передаем только balances без actor_ids
        }
    }

    // Store non-zero balances in the database
    for (const auto& pair : actor_token_set) {
        auto it = balances.find(pair);
        if (it != balances.end() && it->second != BigNumberFloat(0)) {
            write_cached_balance(pair.first, pair.second, it->second);
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

void DagCache::process_transaction(const Transaction& tx, Balances& balances) {
    // Skip if transaction doesn't affect balances
    if (tx.type() == TransactionType::Unknown) {
        return;
    }

    // Reward transactions
    if (tx.type() == TransactionType::Reward && !tx.sender().is_zero()) {
        auto key = std::make_pair(tx.sender(), tx.token());
        if (balances.find(key) == balances.end()) {
            balances[key] = BigNumberFloat(0);
        }
        balances[key] += tx.amount();
    }
    // Contract initialization
    else if (tx.type() == TransactionType::InitContract && !tx.sender().is_zero() && !tx.token().is_zero()) {
        auto key = std::make_pair(tx.sender(), tx.token());
        if (balances.find(key) == balances.end()) {
            balances[key] = BigNumberFloat(0);
        }
        balances[key] += tx.amount();
    }
    // Token conversion
    else if (tx.type() == TransactionType::Conversion && !tx.sender().is_zero()) {
        if (tx.meta().has_value()) {
            auto from_token = TokenId::create(tx.meta().value());
            if (from_token.has_value()) {
                // Deduct from source token
                auto from_key = std::make_pair(tx.sender(), from_token.value());
                if (balances.find(from_key) == balances.end()) {
                    balances[from_key] = BigNumberFloat(0);
                }
                balances[from_key] -= tx.amount();

                // Add to destination token
                auto to_key = std::make_pair(tx.sender(), tx.token());
                if (balances.find(to_key) == balances.end()) {
                    balances[to_key] = BigNumberFloat(0);
                }
                balances[to_key] += tx.amount();
            }
        }
    }
    // Regular transactions
    else {
        // If receiver is valid, add funds
        if (!tx.receiver().is_zero() && !tx.token().is_zero()) {
            auto key = std::make_pair(tx.receiver(), tx.token());
            if (balances.find(key) == balances.end()) {
                balances[key] = BigNumberFloat(0);
            }
            balances[key] += tx.amount();
        }
        // If sender is valid, deduct funds
        if (!tx.sender().is_zero() && !tx.token().is_zero()) {
            auto key = std::make_pair(tx.sender(), tx.token());
            if (balances.find(key) == balances.end()) {
                balances[key] = BigNumberFloat(0);
            }
            balances[key] -= tx.amount();
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
