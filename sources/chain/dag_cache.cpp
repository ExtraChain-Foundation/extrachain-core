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

#include "chain/dag_cache.h"
#include "chain/dag.h"
#include "managers/extrachain_node.h"
#include "network/network_manager.h"
#include "utils/db_connector.h"

#include "utils/thread_pool_boost.h"

DagCache::DagCache(ExtraChainNode* node, Dag* dag)
    : node(node)
    , dag(dag) {
}

DagCache::~DagCache() {
    // Ensure DB is closed
    if (cache_db_ && cache_db_->is_open()) {
        cache_db_->close();
    }
}

BigNumber DagCache::section() const {
    return cached_section_;
}

void DagCache::set_section(const SectionId& section_id, Force force) {
    if (force == Force::None && cached_section_ >= section_id) {
        return;
    }

    cached_section_ = section_id;
}

std::pair<SectionId, Balances> DagCache::read_cached_balances() {
    Balances balances;

    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for read_cached_balances");
        return { BigNumber(-1), balances }; // TODO: expected
    }

    const auto rows          = cache_db_->select("SELECT * FROM balance_cache");
    auto       cache_section = cached_section_;

    for (const auto& row : rows) {
        try {
            auto actor_id = ActorId::create(row.at("actor_id"));
            auto token_id = TokenId::create(row.at("token_id"));
            auto balance  = BigNumberFloat::create(row.at("balance"));

            if (!actor_id.has_value() || !token_id.has_value() || !balance.has_value()) {
                continue;
            }

            balances[{ actor_id.value(), token_id.value() }] = balance.value();
        } catch (const std::out_of_range& e) {
            eLog("[DagCache] Missing required field in database row: {}", e.what());
            continue;
        }
    }

    return { cache_section, balances };
}

std::optional<std::pair<SectionId, Balances>> DagCache::read_cached_balances(
    const std::vector<std::pair<ActorId, TokenId>>& actor_token_pairs) {
    Balances balances;

    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for read_cached_balance");
        return std::nullopt;
    }

    if (actor_token_pairs.empty()) {
        return std::pair { cached_section_, balances };
    }

    const size_t PAIRS_PER_QUERY = 100;
    auto         cache_section   = cached_section_;

    for (size_t i = 0; i < actor_token_pairs.size(); i += PAIRS_PER_QUERY) {
        size_t                                   end_idx = std::min(i + PAIRS_PER_QUERY, actor_token_pairs.size());
        std::vector<std::pair<ActorId, TokenId>> pairs_chunk(actor_token_pairs.begin() + i,
                                                             actor_token_pairs.begin() + end_idx);

        std::string              query = "SELECT * FROM balance_cache WHERE ";
        std::vector<std::string> conditions;

        for (const auto& pair : pairs_chunk) {
            conditions.push_back(fmt::format("(actor_id = \"{}\" AND token_id = \"{}\")",
                                             pair.first.to_string(),
                                             pair.second.to_string()));
        }

        query += boost::algorithm::join(conditions, " OR ");

        const auto rows = cache_db_->select(query, "balance_cache");

        for (const auto& row : rows) {
            auto actor_id = ActorId::create(row.at("actor_id"));
            auto token_id = TokenId::create(row.at("token_id"));
            auto balance  = BigNumberFloat::create(row.at("balance"));
            if (!actor_id.has_value() || !token_id.has_value() || !balance.has_value()) {
                continue;
            }
            balances[{ actor_id.value(), token_id.value() }] = balance.value();
        }
    }

    return std::pair { cache_section, balances };
}

std::optional<Balances> DagCache::get_cached_balances_for_actors(const std::vector<ActorId>& actor_ids) {
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for get_cached_balances_for_actors");
        return std::nullopt;
    }

    if (actor_ids.empty()) {
        return Balances {};
    }

    Balances balances;

    const size_t ACTORS_PER_QUERY = 100;

    for (size_t i = 0; i < actor_ids.size(); i += ACTORS_PER_QUERY) {
        size_t               end_idx = std::min(i + ACTORS_PER_QUERY, actor_ids.size());
        std::vector<ActorId> actor_chunk(actor_ids.begin() + i, actor_ids.begin() + end_idx);

        std::string              query = "SELECT * FROM balance_cache WHERE actor_id IN (";
        std::vector<std::string> actor_ids_str;
        for (const auto& actor_id : actor_chunk) {
            actor_ids_str.push_back("'" + actor_id.to_string() + "'");
        }
        query += boost::algorithm::join(actor_ids_str, ", ");
        query += ")";

        const auto rows = cache_db_->select(query, "balance_cache");

        for (const auto& row : rows) {
            auto actor_id = ActorId::create(row.at("actor_id"));
            auto token_id = TokenId::create(row.at("token_id"));
            auto balance  = BigNumberFloat::create(row.at("balance"));
            if (!actor_id.has_value() || !token_id.has_value() || !balance.has_value()) {
                continue;
            }
            balances[{ actor_id.value(), token_id.value() }] = balance.value();
        }
    }

    return balances;
}

void DagCache::write_cached_balances(const Balances& balances, const std::optional<SectionId>& section_id) {
    // Check if database is initialized
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for write_cached_balances");
        return;
    }

    // Lock mutex to protect transaction block from concurrent access
    std::unique_lock<std::mutex> lock(mutex_);

    // Start a transaction for efficiency
    cache_db_->query("BEGIN TRANSACTION");

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
        cache_db_->replace("balance_cache", data);
        // }
    }

    // Commit transaction
    cache_db_->query("COMMIT");

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

    auto rows = cache_db_->select(
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
    cache_db_->replace("balance_cache", data);
    // }
}

BigNumber DagCache::calculate_genesis_section(const SectionId& section_id) const {
    // Calculate the genesis section (multiple of Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS)
    return (section_id / Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS)
           * Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
}

Balances DagCache::calculate_balances(const std::vector<ActorId>& actor_ids,
                                      const SectionId&            current_section,
                                      const SectionId&            first_saved_section,
                                      std::optional<SectionId>    to_section) {
    // eLog("[DagCache] Calculating balances for {} actors...", actor_ids.size());
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

        auto cached_balances_opt = get_cached_balances_for_actors(actor_ids);
        if (cached_balances_opt.has_value()) {
            balances = cached_balances_opt.value();
        }
    } else {
        balance_start_section = first_saved_section;
    }

    // Process transactions after the balance_start_section up to current_section
    auto to = to_section.has_value() ? to_section.value() : current_section;
    for (BigNumber i = balance_start_section; i <= to; i++) {
        auto section = dag->read_section(i);
        if (!section.has_value()) {
            continue;
        }
        if (section->transactions.empty() || section->id < 0) {
            continue;
        }

        // Process each transaction in the section
        for (const auto& tx : section->transactions) {
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

    std::erase_if(balances, [&actor_ids](const auto& balance_entry) {
        const ActorId& actor_id = balance_entry.first.first;
        return std::find(actor_ids.begin(), actor_ids.end(), actor_id) == actor_ids.end();
    });

    eLog("[Dag] Calculating balances: {}", balances);

    return balances;
}

CacheResult DagCache::check_and_update_cache(const SectionId& current_section) {
    // Calculate safe section ID based on lag
    // We only want to cache sections that are at least CACHE_LAG_SECTIONS behind the current section
    // BigNumber cache_boundary = (current_section / 20) * 20;
    // BigNumber threshold      = cache_boundary + CACHE_LAG_SECTIONS;

    // if (current_section < threshold) {
    if (current_section < BigNumber(CACHE_LAG_SECTIONS)) {
        // eLog("[DagCache] Not enough sections for caching: current={}, required lag={}",
        //      current_section,
        //      CACHE_LAG_SECTIONS);
        return CacheResult { .result = false, .from = BigNumber(-1), .to = BigNumber(-1) };
    }

    // First, calculate the section with lag
    BigNumber safe_section_with_lag = current_section - CACHE_LAG_SECTIONS;

    // Then, find the nearest genesis section (multiple of CONSTRUCT_GENESIS_EVERY_BLOCKS)
    BigNumber safe_genesis_section = calculate_genesis_section(safe_section_with_lag);

    if (safe_genesis_section > dag->current_section()) {
        return CacheResult { .result = false, .from = BigNumber(-1), .to = BigNumber(-1) };
    }

    // eLog("[DagCache] Checking cache update: current={}, with_lag={}, cached={}, safe_genesis={}",
    //      current_section,
    //      safe_section_with_lag,
    //      cached_section_,
    //      safe_genesis_section);

    // Don't update if already at or ahead of safe section
    if (cached_section_ != BigNumber(-1) && safe_genesis_section <= cached_section_) {
        // eLog("[DagCache] No cache update needed: safe_genesis_section <= cached_section_");
        return CacheResult { .result = false, .from = BigNumber(-1), .to = BigNumber(-1) };
    }

    // Don't update if would be moving backwards
    if (cached_section_ > safe_genesis_section) {
        eLog("[DagCache] Invalid cache update: cached_section_ > safe_genesis_section");
        return CacheResult { .result = false, .from = BigNumber(-1), .to = BigNumber(-1) };
    }

    // Don't update if the distance between the current cache section and the new safe section
    // is less than CACHE_LAG_SECTIONS (to prevent frequent updates)
    if (cached_section_ != BigNumber(-1)
        && (safe_genesis_section - cached_section_) < BigNumber(CACHE_LAG_SECTIONS)) {
        // eLog("[DagCache] Skipping cache update: not enough new sections since last update");
        return CacheResult { .result = false, .from = BigNumber(-1), .to = BigNumber(-1) };
    }

    // eLog("[DagCache] Cache update needed: current section = {}, cached section = {}, safe genesis = {}",
    //      current_section,
    //      cached_section_,
    //      safe_genesis_section);

    // Use read_section callback from DAG
    auto read_section_callback = [this](const BigNumber& section_id) -> std::optional<Section> {
        return dag->read_section(section_id);
    };

    // Update cache to safe section (genesis + lag)
    auto [result, start_section] = this->update_to_genesis_section(safe_genesis_section,
                                                                   current_section,
                                                                   dag->first_saved_section(),
                                                                   read_section_callback);

    if (result) {
        // Update the section range to reflect new cache
        dag->update_range();
        return CacheResult { .result = true, .from = start_section, .to = safe_genesis_section };
    }

    return CacheResult { .result = false, .from = BigNumber(-1), .to = BigNumber(-1) };
}

void DagCache::check_and_update_cache_thread(const SectionId& current_section) {
    if (dag == nullptr) {
        return;
    }
    if (dag->status() != DagStatus::Ready) {
        // ThreadPoolBoost::instance()->post([this] { // remove
        auto res = this->check_and_update_cache(dag->current_section());

        if (res.result) {
            dag->update_range();
            // return;
            node->dag()->generate_hash_from_section(res.from);

            auto control_hash = node->dag()->read_control(res.to);
            if (!control_hash.has_value()) {
                eTemp("[DagCache] Problem with control hash from {}", res.to);
                dag->start_control(Force::Active, Force::None);
                node->dag()->generate_hash_from_section(res.from);

                auto control_hash = node->dag()->read_control(res.to);
                if (!control_hash.has_value()) {
                    eCritical("[DagCache] Problem with control hash from {}", res.to);
                }
            }
        }
        // });
    } else {
        auto res = this->check_and_update_cache(dag->current_section());

        if (res.result) {
            dag->update_range();

            if (dag->mode() == DagMode::Light) {
                auto last_hash    = node->dag()->generate_hash_from_section(res.from);
                auto control_hash = node->dag()->read_control(res.to);
                if (!control_hash.has_value()) {
                    eCritical("[DagCache] Problem with control hash from {}", res.to);
                    return;
                }
                if (!last_hash.has_value()) {
                    eCritical("[DagCache] No last hash");
                    return;
                }

                auto hash_interval = HashInterval { .from = res.from, .to = res.to, .hash = last_hash.value() };
                eLog("[Dag] Cache from {} to {}", res.from.to_int(), res.to.to_int());
                // eLog("[Dag] Send {}", hash_interval);
                node->network()->send_message(hash_interval, MessageType::DagIntervalHash, SendMode::Neighbours);
                return;
            }

            // ThreadPoolBoost::instance()->post([this, res] {
            auto last_hash    = node->dag()->generate_hash_from_section(res.from);
            auto control_hash = node->dag()->read_control(res.to);
            if (!control_hash.has_value()) {
                eCritical("[DagCache] Problem with control hash from {}", res.to);
                return;
            }
            if (!last_hash.has_value()) {
                eCritical("[DagCache] No last hash");
                return;
            }

            auto hash_interval = HashInterval { .from = res.from, .to = res.to, .hash = last_hash.value() };
            eLog("[Dag] Cache from {} to {}", res.from.to_int(), res.to.to_int());
            // eLog("[Dag] Send {}", hash_interval);
            node->network()->send_message(hash_interval, MessageType::DagIntervalHash, SendMode::Neighbours);
            // });
        }
    }
}

std::pair<bool, SectionId> DagCache::update_to_genesis_section(
    const SectionId&                                        genesis_section,
    const SectionId&                                        current_section,
    const SectionId&                                        first_saved_section,
    std::function<std::optional<Section>(const SectionId&)> read_section_callback) {
    // If trying to update to same section, nothing to do
    if (cached_section_ == genesis_section) {
        return { true, BigNumber(-1) };
    }

    // Initialize DB
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize DB for update_to_genesis_section");
        return { false, BigNumber(-1) };
    }

    // Lock mutex to protect transaction block from concurrent access
    std::unique_lock<std::mutex> lock(mutex_);

    bool show = dag->status_ == DagStatus::Sync ? genesis_section % 500 == 0 : true;
    if (show) {
        eLog("[DagCache] Updating cache to genesis section: {}", genesis_section);
    }

    SectionId start_section;
    if (cached_section_ != BigNumber(-1)) {
        start_section = cached_section_ + 1;
        // if (cached_section_ == 0 && current_section < 20) {
        //     start_section = 0; // ?
        // }
    } else {
        start_section = first_saved_section;
    }

    std::set<std::pair<ActorId, TokenId>> actor_token_set;

    // Scan from start_section to genesis_section to collect actor-token pairs
    for (BigNumber i = start_section; i <= genesis_section; i++) {
        auto section = read_section_callback(i);
        if (!section.has_value()) {
            continue;
        }
        if (section->transactions.empty()) {
            continue;
        }

        if (i % BigNumber(20000) == 0) {
            eLog("update_to_genesis_section scan on {}", i.to_printable_string());
        }

        for (const auto& tx : section->transactions) {
            // if (tx.verify()) {

            // }

            actor_token_set.insert({ tx.sender(), tx.token() });
            actor_token_set.insert({ tx.receiver(), tx.token() });

            if (tx.type() == TransactionType::Conversion && tx.meta().has_value()) {
                auto from_token = TokenId::create(tx.meta().value());
                if (from_token.has_value()) {
                    actor_token_set.insert({ tx.sender(), from_token.value() });
                    actor_token_set.insert({ tx.receiver(), from_token.value() });
                }
            }
        }
    }

    // eLog("[DagCache] Found {} unique actor-token pairs for caching", actor_token_set.size());

    // Start a transaction for efficiency
    cache_db_->query("BEGIN TRANSACTION");

    std::vector<std::pair<ActorId, TokenId>> actor_token_pairs(actor_token_set.begin(), actor_token_set.end());

    // Balances from cache
    auto cached_balances_opt = read_cached_balances(actor_token_pairs);
    if (!cached_balances_opt.has_value()) {
        eLog("[DagCache] Failed to read cached balances");
        cache_db_->query("ROLLBACK");
        return { false, BigNumber(-1) };
    }

    Balances& balances  = cached_balances_opt.value().second;
    auto      cache_res = read_cached_balances();                 // ?
    local_clear_less_balances(cache_res.first, cache_res.second); // ?

    // Process all transactions from start_section to genesis_section
    for (BigNumber i = start_section; i <= genesis_section; i++) {
        auto section = read_section_callback(i);
        if (!section.has_value()) {
            continue;
        }
        if (section->transactions.empty()) {
            continue;
        }

        // Process each transaction
        for (const auto& tx : section->transactions) {
            process_transaction(tx, balances);

            if (dag->mode() == DagMode::Full) {
                // write_index(tx.sender(), tx.receiver(), tx.section(), tx.timestamp());
            }
        }
    }

    // Store non-zero balances in the database
    for (const auto& pair : actor_token_set) {
        auto it = balances.find(pair);
        if (it != balances.end() /*&& it->second != BigNumberFloat(0)*/) {
            write_cached_balance(pair.first, pair.second, it->second);
        }
    }

    // Commit transaction
    cache_db_->query("COMMIT");
    // Update cached section
    // cached_section_ = genesis_section;
    set_section(genesis_section);
    // eLog("[DagCache] Cache updated to section {}", cached_section_);
    dag->update_range();
    return { true, start_section };
}

void DagCache::process_transaction(const Transaction& tx, Balances& balances) {
    // Skip if transaction doesn't affect balances
    if (tx.type() == TransactionType::Unknown) {
        return;
    }

    // Minting transactions (creates from nothing, adds to receiver)
    if (tx.type() == TransactionType::Minting && !tx.receiver().is_zero() && !tx.token().is_zero()) {
        auto key = std::make_pair(tx.receiver(), tx.token());

        balances[key] += tx.amount();
        return;
    }

    // Reward transactions
    if (tx.type() == TransactionType::Reward && !tx.sender().is_zero()) {
        auto key = std::make_pair(tx.sender(), tx.token());

        balances[key] += tx.amount();
    }
    // Contract initialization
    else if (tx.type() == TransactionType::InitContract && !tx.sender().is_zero() && !tx.token().is_zero()) {
        auto key = std::make_pair(tx.sender(), tx.token());

        balances[key] += tx.amount();
    }
    // Token conversion
    else if (tx.type() == TransactionType::Conversion && !tx.sender().is_zero()) {
        if (tx.meta().has_value()) {
            auto from_token = TokenId::create(tx.meta().value());
            if (from_token.has_value()) {
                // Deduct from source token
                auto from_key = std::make_pair(tx.sender(), from_token.value());

                balances[from_key] -= tx.amount();

                // Add to destination token
                auto to_key = std::make_pair(tx.sender(), tx.token());

                balances[to_key] += tx.amount();
            }
        }
    }
    // Regular transactions
    else {
        // If receiver is valid, add funds
        if (!tx.receiver().is_zero() && !tx.token().is_zero()) {
            auto key = std::make_pair(tx.receiver(), tx.token());

            balances[key] += tx.amount();
        }
        // If sender is valid, deduct funds
        if (!tx.sender().is_zero() && !tx.token().is_zero()) {
            auto key = std::make_pair(tx.sender(), tx.token());

            balances[key] -= tx.amount();
        }
    }
}

bool DagCache::init_db() {
    if (db_initialized_) {
        return true;
    }

    if (cache_db_ && cache_db_->is_open()) {
        db_initialized_ = true;
        return true;
    }

    std::unique_lock<std::mutex> lock(mutex_);

    // bool is_exists = QFile(QString::fromStdString(ChainConst::BALANCE_CACHE)).exists();

    QDir().mkdir(QString::fromStdString(ChainConst::DAG_FOLDER));
    QDir().mkdir(QString::fromStdString(ChainConst::DAG_CACHE_FOLDER));

    std::string db_path = ChainConst::BALANCE_CACHE;
    cache_db_           = std::make_unique<DbConnector>(db_path);

    if (!cache_db_->open()) {
        eLog("[DagCache] Failed to open cache database");

        return false;
    }

    // Create table if it doesn't exist
    bool success = cache_db_->query(Config::DataStorage::DagCacheCreate);

    if (!success) {
        eLog("[DagCache] Failed to create cache table");
        return false;
    }

    // if (!is_exists && node_->dag()->current_section() > Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS) {
    //     BigNumber safe_genesis_section = calculate_genesis_section(node_->dag()->current_section());

    //     // Use read_section callback from DAG
    //     auto read_section_callback = [this](const BigNumber& section_id) -> std::optional<Section> {
    //         return node_->dag()->read_section(section_id);
    //     };

    //     // Update cache to safe section (genesis + lag)
    //     bool result = update_to_genesis_section(safe_genesis_section,
    //                                             node_->dag()->current_section(),
    //                                             node_->dag()->first_saved_section(),
    //                                             read_section_callback);
    // }

    eLog("[DagCache] Cache database initialized");
    db_initialized_ = true;

    return true;
}

void DagCache::reset_db() {
    std::unique_lock<std::mutex> lock(mutex_);
    db_initialized_ = false;
    if (db_initialized_) {
        cache_db_->close();
        QFile::remove(ChainConst::BALANCE_CACHE.c_str());
    }
    cache_db_.reset();
}

void reverse_transaction(const Transaction& tx, Balances& balances) {
    // Skip if transaction doesn't affect balances
    if (tx.type() == TransactionType::Unknown) {
        return;
    }

    // Reward transactions - reverse: subtract amount from sender
    if (tx.type() == TransactionType::Reward && !tx.sender().is_zero()) {
        auto key = std::make_pair(tx.sender(), tx.token());
        balances[key] -= tx.amount();
    }
    // Contract initialization - reverse: subtract amount from sender
    else if (tx.type() == TransactionType::InitContract && !tx.sender().is_zero() && !tx.token().is_zero()) {
        auto key = std::make_pair(tx.sender(), tx.token());
        balances[key] -= tx.amount();
    }
    // Token conversion - reverse: add back to source, subtract from destination
    else if (tx.type() == TransactionType::Conversion && !tx.sender().is_zero()) {
        if (tx.meta().has_value()) {
            auto from_token = TokenId::create(tx.meta().value());
            if (from_token.has_value()) {
                // Add back to source token (reverse of deduction)
                auto from_key = std::make_pair(tx.sender(), from_token.value());
                balances[from_key] += tx.amount();

                // Subtract from destination token (reverse of addition)
                auto to_key = std::make_pair(tx.sender(), tx.token());
                balances[to_key] -= tx.amount();
            }
        }
    }
    // Regular transactions - reverse: subtract from receiver, add to sender
    else {
        // If receiver is valid, subtract funds (reverse of addition)
        if (!tx.receiver().is_zero() && !tx.token().is_zero()) {
            auto key = std::make_pair(tx.receiver(), tx.token());
            balances[key] -= tx.amount();
        }

        // If sender is valid, add funds back (reverse of deduction)
        if (!tx.sender().is_zero() && !tx.token().is_zero()) {
            auto key = std::make_pair(tx.sender(), tx.token());
            balances[key] += tx.amount();
        }
    }
}

std::set<ActorId> DagCache::local_clear_less_balances(const SectionId& from, const Balances& start_balances) {
    auto              balances = start_balances;
    std::set<ActorId> actors;

    // eLog("[Dag] Clear txs...");

    for (auto i = from; i <= dag->current_section(); i++) {
        auto section = dag->read_section(i);
        if (!section.has_value()) {
            continue;
        }
        if (section->transactions.empty() || section->id < 0) {
            continue;
        }

        // Process each transaction in the section
        for (const auto& tx : section->transactions) {
            process_transaction(tx, balances);

            if (tx.section() <= 1) {
                continue;
            }

            // if (tx.sender() == ActorId("1a902514053b9f2c814621799acbbef21e2ff6a5")
            //     || tx.receiver() == ActorId("1a902514053b9f2c814621799acbbef21e2ff6a5")) {
            //     auto tx1 = tx;
            //     tx1.set_prev_hashs({ "hashs: " + std::to_string(tx1.prev_hashs().size()) });
            //     eLog("{}\n", tx1);
            // }

            if (tx.type() == TransactionType::Reward || tx.type() == TransactionType::Conversion) {
                continue;
            }

            if (balances[{ tx.sender(), tx.token() }] < 0) {
#ifndef IS_APP_UI_CLIENT
                eInfo(
                    "[Dag] Exorcised. Section: {}, sender: {}, receiver: {}, type: {}, token: {}, amount: {}, "
                    "balance: {}, timestamp: {}",
                    tx.section(),
                    tx.sender(),
                    tx.receiver(),
                    tx.type(),
                    tx.token(),
                    tx.amount(),
                    balances[{ tx.sender(), tx.token() }],
                    tx.timestamp());
#endif

                reverse_transaction(tx, balances);
                dag->local_remove_transaction(tx.section(), tx.hash());
                actors.insert(tx.sender());
            }
        }
    }

    if (actors.size() != 0) {
        eLog("[Dag] Removed for actors: {}", actors);
    }
    return actors;
}
