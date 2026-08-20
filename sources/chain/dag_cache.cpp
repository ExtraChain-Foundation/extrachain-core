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
#include "contracts/contract_codec.h"
#include "contracts/contract_transaction.h"
#include "core/extrachain_node.h"
#include "network/network_service.h"
#include "utils/db_connector.h"

#include <msgpack.hpp>

namespace {
    using ContractDelta = std::pair<ActorId, BigNumberFloat>;

    std::vector<ContractDelta> fungible_contract_deltas(const Transaction& transaction) {
        if (!is_contract_transaction(transaction.type()) || !transaction.meta().has_value()) {
            return {};
        }
        auto metadata = Json::deserialize<ContractTransactionData>(*transaction.meta());
        if (!metadata.has_value() || metadata->kind != "fungible-token") {
            return {};
        }
        auto encoded = Utils::from_base64<std::vector<std::uint8_t>>(metadata->effects_base64);
        if (!encoded.has_value()) {
            return {};
        }
        auto effects = ExtraChain::Contracts::Codec::decode_effects(encoded.value());
        if (!effects.has_value()
            || ExtraChain::Contracts::Codec::effect_hash(effects.value()) != metadata->effects_hash) {
            return {};
        }

        auto amount = [](const std::string& value, bool positive) {
            auto result = BigNumberFloat(value);
            return positive ? result : -result;
        };
        auto actor = [](std::string value) -> std::optional<ActorId> {
            auto result = ActorId::create(std::move(value));
            if (!result.has_value()) {
                return std::nullopt;
            }
            return *result;
        };

        try {
            std::vector<ContractDelta> result;
            for (const auto& effect : effects.value()) {
                if (effect.kind != ExtraChain::Contracts::ContractEffectKind::TokenDelta
                    || effect.target != transaction.receiver().to_string()) {
                    continue;
                }
                auto handle = msgpack::unpack(reinterpret_cast<const char*>(effect.arguments.data()),
                                              effect.arguments.size());
                auto object = handle.get();
                if (effect.operation == "mint" || effect.operation == "burn") {
                    std::vector<std::tuple<std::string, std::string>> entries;
                    object.convert(entries);
                    if (entries.size() != 1) {
                        return {};
                    }
                    auto owner = actor(std::get<0>(entries.front()));
                    if (!owner.has_value()) {
                        return {};
                    }
                    result.emplace_back(owner.value(),
                                        amount(std::get<1>(entries.front()), effect.operation == "mint"));
                } else if (effect.operation == "transfer") {
                    std::vector<std::tuple<std::string, std::string>> entries;
                    object.convert(entries);
                    if (entries.size() != 2 || std::get<1>(entries[0]) != std::get<1>(entries[1])) {
                        return {};
                    }
                    auto sender   = actor(std::get<0>(entries[0]));
                    auto receiver = actor(std::get<0>(entries[1]));
                    if (!sender.has_value() || !receiver.has_value()) {
                        return {};
                    }
                    result.emplace_back(sender.value(), amount(std::get<1>(entries[0]), false));
                    result.emplace_back(receiver.value(), amount(std::get<1>(entries[1]), true));
                }
            }
            return result;
        } catch (const std::exception&) {
            return {};
        }
    }

    void apply_contract_deltas(const Transaction& transaction, Balances& balances, bool reverse) {
        for (auto& [actor_id, delta] : fungible_contract_deltas(transaction)) {
            balances[{ actor_id, transaction.receiver() }] += reverse ? -delta : delta;
        }
    }
} // namespace

DagCache::DagCache(ExtraChain::Core::ExtraChainNode* node, Dag* dag)
    : node(node)
    , dag(dag) {
}

DagCache::~DagCache() {
    std::lock_guard lock(mutex_);
    // Ensure DB is closed
    if (cache_db_ && cache_db_->is_open()) {
        cache_db_->close();
    }
}

BigNumber DagCache::section() const {
    std::lock_guard lock(mutex_);
    return cached_section_;
}

std::pair<SectionId, Balances> DagCache::read_cached_balances() {
    std::lock_guard lock(mutex_);
    Balances        balances;

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
    std::lock_guard lock(mutex_);
    Balances        balances;

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
    std::lock_guard lock(mutex_);
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

bool DagCache::write_cached_balances(const Balances& balances, const SectionId& section_id) {
    std::lock_guard lock(mutex_);
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize db for write_cached_balances");
        return false;
    }

    if (!cache_db_->query("BEGIN IMMEDIATE TRANSACTION") || !cache_db_->query("DELETE FROM balance_cache")) {
        static_cast<void>(cache_db_->query("ROLLBACK"));
        return false;
    }

    for (const auto& [key, balance] : balances) {
        if (!write_cached_balance(key.first, key.second, balance)) {
            static_cast<void>(cache_db_->query("ROLLBACK"));
            return false;
        }
    }

    if (!write_cache_section(section_id) || !cache_db_->query("COMMIT")) {
        static_cast<void>(cache_db_->query("ROLLBACK"));
        return false;
    }

    cached_section_ = section_id;
    eLog("[DagCache] Wrote {} balances to cache", balances.size());
    return true;
}

BigNumberFloat DagCache::read_cached_balance(const ActorId& actor_id, const TokenId& token_id) {
    std::lock_guard lock(mutex_);
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

bool DagCache::write_cached_balance(const ActorId&        actor_id,
                                    const TokenId&        token_id,
                                    const BigNumberFloat& balance) {
    if (balance == 0) {
        return cache_db_ != nullptr
               && cache_db_->delete_row("balance_cache",
                                        { { "actor_id", actor_id.to_string() },
                                          { "token_id", token_id.to_string() } });
    }
    DbRow data = { { "actor_id", actor_id.to_string() },
                   { "token_id", token_id.to_string() },
                   { "balance", balance.to_string() } };
    return cache_db_ != nullptr && cache_db_->replace("balance_cache", data);
}

bool DagCache::write_cache_section(const SectionId& section_id) {
    return cache_db_ != nullptr
           && cache_db_->replace("balance_cache_meta", { { "id", "1" }, { "section", section_id.to_string() } });
}

bool DagCache::clear_balance_snapshot() {
    if (cache_db_ == nullptr || !cache_db_->query("BEGIN IMMEDIATE TRANSACTION")
        || !cache_db_->query("DELETE FROM balance_cache") || !cache_db_->query("DELETE FROM balance_cache_meta")
        || !write_cache_section(SectionId(-1)) || !cache_db_->query("COMMIT")) {
        if (cache_db_ != nullptr) {
            static_cast<void>(cache_db_->query("ROLLBACK"));
        }
        return false;
    }
    cached_section_ = SectionId(-1);
    return true;
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

    const auto target_section = to_section.value_or(current_section);
    const bool live_request =
        dag != nullptr && dag->status() == DagStatus::Ready && target_section >= current_section;
    if (live_request) {
        std::lock_guard lock(live_balance_mutex_);
        const bool      complete = live_balance_section_ == current_section
                              && std::ranges::all_of(actor_ids, [this](const ActorId& actor) {
                                     return live_balance_actors_.contains(actor);
                                 });
        if (complete) {
            balances = live_balances_;
            std::erase_if(balances, [&actor_ids](const auto& entry) {
                return std::ranges::find(actor_ids, entry.first.first) == actor_ids.end();
            });
            return balances;
        }
    }

    BigNumber balance_start_section;

    {
        std::lock_guard lock(mutex_);
        // Check if we have a valid cache that we can use
        // The cache holds balances as of cached_section_, so it can only seed a query
        // that asks for that point or later. If it has already moved past
        // target_section its balances include transactions the caller explicitly
        // excluded, and the loop below only adds — it cannot take them back out.
        // Two nodes whose caches sit at different sections would then answer the same
        // historical query differently, which breaks anything comparing balances
        // across nodes.
        if (cached_section_ != BigNumber(-1) && cached_section_ <= target_section) {
            // We have some cache, which may be at an earlier point than the genesis_section
            balance_start_section = cached_section_ + 1;

            auto cached_balances_opt = get_cached_balances_for_actors(actor_ids);
            if (cached_balances_opt.has_value()) {
                balances = cached_balances_opt.value();
            }
        } else {
            balance_start_section = first_saved_section;
        }
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

    if (live_request && dag->current_section() == current_section) {
        std::lock_guard lock(live_balance_mutex_);
        if (live_balance_section_ != current_section) {
            live_balances_.clear();
            live_balance_actors_.clear();
            live_balance_section_ = current_section;
        }
        for (const auto& actor : actor_ids) {
            std::erase_if(live_balances_, [&actor](const auto& entry) {
                return entry.first.first == actor;
            });
            live_balance_actors_.insert(actor);
        }
        live_balances_.insert(balances.begin(), balances.end());
    }

    return balances;
}

void DagCache::apply_live_transaction(const Transaction& transaction) {
    std::lock_guard lock(live_balance_mutex_);
    if (live_balance_actors_.empty())
        return;
    if (live_balance_section_ > transaction.section()
        || live_balance_section_ + BigNumber(1) < transaction.section()) {
        live_balances_.clear();
        live_balance_actors_.clear();
        live_balance_section_ = SectionId(-1);
        return;
    }
    process_transaction(transaction, live_balances_);
    live_balance_section_ = transaction.section();
}

void DagCache::apply_live_transactions(const std::vector<Transaction>& transactions) {
    if (transactions.empty())
        return;

    std::lock_guard lock(live_balance_mutex_);
    if (live_balance_actors_.empty())
        return;

    for (const auto& transaction : transactions) {
        if (live_balance_section_ > transaction.section()
            || live_balance_section_ + BigNumber(1) < transaction.section()) {
            live_balances_.clear();
            live_balance_actors_.clear();
            live_balance_section_ = SectionId(-1);
            return;
        }
        process_transaction(transaction, live_balances_);
        live_balance_section_ = transaction.section();
    }
}

void DagCache::apply_transaction_delta(const Transaction& transaction, Balances& balances) {
    process_transaction(transaction, balances);
}

void DagCache::invalidate_live_balances() {
    std::lock_guard lock(live_balance_mutex_);
    live_balances_.clear();
    live_balance_actors_.clear();
    live_balance_section_ = SectionId(-1);
}

CacheResult DagCache::check_and_update_cache(const SectionId& current_section) {
    std::lock_guard  update_lock(update_mutex_);
    std::unique_lock cache_lock(mutex_);

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
        cache_lock.unlock();
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

    // Cache data and controls use the same closed boundary. A node can stop after
    // committing the cache and before writing its derived control. A later cache
    // pass then reports "no update" and used to leave that control absent forever.
    // Repair only the cached boundary. Do not call start_control() here: its broad
    // search is not safe to run beside active admission and it repeats valid work.
    if (dag->mode() == DagMode::Full) {
        const auto cache_tip = section();
        if (cache_tip >= SectionId(0) && !dag->read_control(cache_tip).has_value()) {
            const auto start = cache_tip == SectionId(0) ? SectionId(0) : cache_tip - CONTROL_INTERVAL_DIFF;
            const auto hash  = dag->generate_hash_from_section(start, Force::Active, Force::None);
            if (!hash.has_value() || !dag->read_control(cache_tip).has_value()) {
                eWarning("[DagCache] Control repair deferred at cached section {}", cache_tip);
            }
        }
    }
}

std::pair<bool, SectionId> DagCache::update_to_genesis_section(
    const SectionId&                                        genesis_section,
    const SectionId&                                        current_section,
    const SectionId&                                        first_saved_section,
    std::function<std::optional<Section>(const SectionId&)> read_section_callback) {
    std::lock_guard cache_lock(mutex_);

    // If trying to update to same section, nothing to do
    if (cached_section_ == genesis_section) {
        return { true, BigNumber(-1) };
    }

    // Initialize DB
    if (!init_db()) {
        eLog("[DagCache] Failed to initialize DB for update_to_genesis_section");
        return { false, BigNumber(-1) };
    }

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

    auto hot_sections = dag->read_hot_sections(start_section, genesis_section);

    if (!cache_db_->query("BEGIN IMMEDIATE TRANSACTION")) {
        return { false, start_section };
    }

    // The stored balances are only a valid starting point when the replay actually
    // continues from where they left off. With no cache section we replay the whole
    // chain from first_saved_section, so seeding from the table would count every
    // one of those transactions twice — the table can still hold rows from an
    // earlier run whose section marker did not survive.
    Balances balances;
    if (cached_section_ != BigNumber(-1)) {
        auto cache_res = read_cached_balances();
        balances       = std::move(cache_res.second);
    } else if (!cache_db_->query("DELETE FROM balance_cache")) {
        static_cast<void>(cache_db_->query("ROLLBACK"));
        return { false, start_section };
    }

    // Process all transactions from start_section to genesis_section
    for (BigNumber i = start_section; i <= genesis_section; i++) {
        auto hot = hot_sections.find(i);
        auto section =
            hot != hot_sections.end() ? std::optional<Section>(std::move(hot->second)) : read_section_callback(i);
        if (!section.has_value()) {
            continue;
        }
        if (section->transactions.empty()) {
            continue;
        }

        // Process each transaction
        for (const auto& tx : section->transactions) {
            process_transaction(tx, balances);

            if (tx.section() > SectionId(1) && tx.type() != TransactionType::Reward
                && tx.type() != TransactionType::Conversion && !is_contract_transaction(tx.type())
                && balances[{ tx.sender(), tx.token() }] < 0) {
                static_cast<void>(cache_db_->query("ROLLBACK"));
                eCritical("[DagCache] State transition {} creates a negative balance at section {}",
                          tx.hash(),
                          tx.section());
                dag->report_state_inconsistency(tx.section(),
                                                "negative-balance-transition",
                                                "balance-cache-replay");
                return { false, start_section };
            }

            if (dag->mode() == DagMode::Full) {
                // write_index(tx.sender(), tx.receiver(), tx.section(), tx.timestamp());
            }
        }
    }

    for (const auto& [key, balance] : balances) {
        if (!write_cached_balance(key.first, key.second, balance)) {
            static_cast<void>(cache_db_->query("ROLLBACK"));
            return { false, start_section };
        }
    }

    if (!write_cache_section(genesis_section) || !cache_db_->query("COMMIT")) {
        static_cast<void>(cache_db_->query("ROLLBACK"));
        return { false, start_section };
    }
    cached_section_ = genesis_section;
    return { true, start_section };
}

std::optional<StateTransitionViolation> DagCache::validate_state_to(const SectionId& current_section) {
    std::lock_guard cache_lock(mutex_);
    auto            cache_res = read_cached_balances();
    // Same rule as the replay in update_to_genesis_section: the stored balances are
    // a valid base only when we continue from the section they were taken at. When
    // there is no cache section we walk the chain from the start, so seeding from
    // the table would double every transaction it already accounts for.
    auto balances = cache_res.first == SectionId(-1) ? Balances {} : std::move(cache_res.second);
    auto from     = cache_res.first == SectionId(-1) ? dag->first_saved_section() : cache_res.first + 1;
    if (from < SectionId(0) || current_section < from) {
        return std::nullopt;
    }

    auto hot_sections = dag->read_hot_sections(from, current_section);
    for (auto section_id = from; section_id <= current_section; ++section_id) {
        const auto hot = hot_sections.find(section_id);
        auto       section =
            hot != hot_sections.end() ? std::optional<Section>(hot->second) : dag->read_section(section_id);
        if (!section.has_value()) {
            continue;
        }
        for (const auto& transaction : section->transactions) {
            process_transaction(transaction, balances);
            if (transaction.section() <= SectionId(1) || transaction.type() == TransactionType::Reward
                || transaction.type() == TransactionType::Conversion
                || is_contract_transaction(transaction.type())) {
                continue;
            }
            if (balances[{ transaction.sender(), transaction.token() }] < 0) {
                return StateTransitionViolation {
                    .section          = transaction.section(),
                    .transaction_hash = transaction.hash(),
                };
            }
        }
    }
    return std::nullopt;
}

void DagCache::process_transaction(const Transaction& tx, Balances& balances) {
    // Skip if transaction doesn't affect balances
    if (tx.type() == TransactionType::Unknown) {
        return;
    }
    if (is_contract_transaction(tx.type())) {
        apply_contract_deltas(tx, balances, false);
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
        if (!tx.receiver().is_zero()) {
            auto key = std::make_pair(tx.receiver(), tx.token());

            balances[key] += tx.amount();
        }
        // If sender is valid, deduct funds
        if (!tx.sender().is_zero()) {
            auto key = std::make_pair(tx.sender(), tx.token());

            balances[key] -= tx.amount();
        }
    }
}

bool DagCache::init_db() {
    std::lock_guard lock(mutex_);
    if (db_initialized_) {
        return true;
    }

    if (cache_db_ && cache_db_->is_open()) {
        db_initialized_ = ensure_balance_cache_schema() && ensure_contract_catalog_schema();
        return db_initialized_;
    }

    std::error_code directory_error;
    std::filesystem::create_directories(ChainConst::DAG_FOLDER, directory_error);
    std::filesystem::create_directories(ChainConst::DAG_CACHE_FOLDER, directory_error);

    std::string db_path = ChainConst::BALANCE_CACHE;
    cache_db_           = std::make_unique<DbConnector>(db_path);

    if (!cache_db_->open()) {
        eLog("[DagCache] Failed to open cache database");

        return false;
    }

    // Create table if it doesn't exist
    bool success = cache_db_->query(Config::DataStorage::DagCacheCreate) && ensure_balance_cache_schema()
                   && ensure_contract_catalog_schema();

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
    invalidate_live_balances();
    std::unique_lock<std::recursive_mutex> lock(mutex_);
    std::unique_lock<std::mutex>           catalog_lock(contract_catalog_mutex_);
    if (!init_db() || !cache_db_->query("BEGIN IMMEDIATE TRANSACTION")
        || !cache_db_->query("DELETE FROM balance_cache") || !cache_db_->query("DELETE FROM balance_cache_meta")
        || !cache_db_->query("DELETE FROM contract_catalog") || !write_cache_section(SectionId(-1))
        || !cache_db_->query("COMMIT")) {
        if (cache_db_ != nullptr) {
            static_cast<void>(cache_db_->query("ROLLBACK"));
        }
        eCritical("[DagCache] Failed to reset derived cache state");
        return;
    }
    contract_catalog_scanned_ = false;
    cached_section_           = SectionId(-1);
}

bool DagCache::ensure_balance_cache_schema() {
    if (cache_db_ == nullptr) {
        return false;
    }

    const bool had_metadata = cache_db_->table_exists("balance_cache_meta");
    if (!cache_db_->query(Config::DataStorage::DagCacheMetadataCreate)) {
        return false;
    }
    if (!had_metadata) {
        return clear_balance_snapshot();
    }

    const auto rows = cache_db_->select("SELECT section FROM balance_cache_meta WHERE id = 1");
    if (rows.size() != 1 || !rows.front().contains("section")) {
        return clear_balance_snapshot();
    }
    const auto section = SectionId::create(rows.front().at("section"));
    if (!section.has_value() || section.value() < SectionId(-1)
        || (dag != nullptr && section.value() > dag->current_section())) {
        return clear_balance_snapshot();
    }
    if (section.value() == SectionId(-1)) {
        return clear_balance_snapshot();
    }
    cached_section_ = section.value();
    return true;
}

bool DagCache::ensure_contract_catalog_schema() {
    if (cache_db_ == nullptr) {
        return false;
    }
    if (cache_db_->table_exists("contract_catalog")) {
        const auto columns            = cache_db_->table_columns("contract_catalog");
        const auto has_current_schema = std::ranges::any_of(columns, [](const DBColumn& column) {
            return column.name == "language";
        });
        if (!has_current_schema && !cache_db_->drop_table("contract_catalog")) {
            return false;
        }
    }
    return cache_db_->query(Config::DataStorage::ContractCatalogCreate);
}

void DagCache::index_contract_transaction(const Transaction& transaction) {
    std::lock_guard lock(mutex_);
    if (!is_contract_transaction(transaction.type()) || !transaction.meta().has_value() || !init_db()) {
        return;
    }

    const auto metadata = Json::deserialize<ContractTransactionData>(*transaction.meta());
    const auto section  = transaction.section().to_int();
    if (!metadata.has_value() || !section.has_value() || metadata->schema != 4 || metadata->kind.empty()
        || metadata->version == 0 || metadata->revision == 0) {
        return;
    }

    const auto contract_id = transaction.receiver().to_string();
    auto       existing    = cache_db_->select("SELECT * FROM contract_catalog WHERE contract_id = ?",
                                      "contract_catalog",
                                               { { "contract_id", contract_id } });

    ExtraChain::Contracts::ContractSummary summary;
    if (transaction.type() == TransactionType::ContractDeploy) {
        if (!existing.empty()) {
            return;
        }
        summary.contract_id             = contract_id;
        summary.owner_id                = transaction.sender().to_string();
        summary.deploy_transaction_hash = transaction.hash();
        summary.deploy_section          = static_cast<std::uint64_t>(*section);
    } else {
        if (existing.empty()) {
            return;
        }
        const auto parsed = Utils::from_dbrow<ExtraChain::Contracts::ContractSummary>(existing.front());
        if (!parsed.has_value() || parsed->owner_id.empty() || metadata->revision <= parsed->revision) {
            return;
        }
        summary = *parsed;
        if (transaction.type() == TransactionType::ContractUpgrade
            && transaction.sender().to_string() != summary.owner_id) {
            return;
        }
    }

    summary.kind             = metadata->kind;
    summary.language         = metadata->language;
    summary.version          = metadata->version;
    summary.revision         = metadata->revision;
    summary.module_hash      = metadata->module_hash;
    summary.state_hash       = metadata->state_hash;
    summary.transaction_hash = transaction.hash();
    summary.section          = static_cast<std::uint64_t>(*section);
    if (metadata->checkpoint) {
        summary.checkpoint_revision         = metadata->revision;
        summary.checkpoint_section          = summary.section;
        summary.checkpoint_state_hash       = metadata->state_hash;
        summary.checkpoint_transaction_hash = transaction.hash();
    }
    summary.replay_depth = summary.revision - summary.checkpoint_revision;
    cache_db_->replace("contract_catalog", Utils::to_dbrow(summary));

    for (const auto& transition : metadata->transitions) {
        auto rows = cache_db_->select("SELECT * FROM contract_catalog WHERE contract_id = ?",
                                      "contract_catalog",
                                      { { "contract_id", transition.contract_id } });
        if (rows.empty()) {
            continue;
        }
        auto nested = Utils::from_dbrow<ExtraChain::Contracts::ContractSummary>(rows.front());
        if (!nested.has_value() || transition.revision <= nested->revision
            || transition.previous_state_hash != nested->state_hash) {
            continue;
        }
        nested->kind             = transition.kind;
        nested->language         = transition.language;
        nested->version          = transition.version;
        nested->revision         = transition.revision;
        nested->module_hash      = transition.module_hash;
        nested->state_hash       = transition.state_hash;
        nested->transaction_hash = transaction.hash();
        nested->section          = static_cast<std::uint64_t>(*section);
        if (transition.checkpoint) {
            nested->checkpoint_revision         = transition.revision;
            nested->checkpoint_section          = nested->section;
            nested->checkpoint_state_hash       = transition.state_hash;
            nested->checkpoint_transaction_hash = transaction.hash();
        }
        nested->replay_depth = nested->revision - nested->checkpoint_revision;
        cache_db_->replace("contract_catalog", Utils::to_dbrow(*nested));
    }
}

ExtraChain::Contracts::ContractCatalogPage DagCache::list_contracts(
    const ExtraChain::Contracts::ContractCatalogFilter& filter) {
    std::lock_guard                            cache_lock(mutex_);
    ExtraChain::Contracts::ContractCatalogPage page;
    if (!init_db()) {
        return page;
    }

    {
        std::unique_lock<std::mutex> lock(contract_catalog_mutex_);
        if (!contract_catalog_scanned_) {
            if (cache_db_->count("contract_catalog") == 0) {
                rebuild_contract_catalog();
            }
            contract_catalog_scanned_ = true;
        }
    }

    auto                                                rows = cache_db_->select_all("contract_catalog");
    std::vector<ExtraChain::Contracts::ContractSummary> matches;
    matches.reserve(rows.size());
    for (const auto& row : rows) {
        auto summary = Utils::from_dbrow<ExtraChain::Contracts::ContractSummary>(row);
        if (!summary.has_value()) {
            continue;
        }
        if (filter.owner_id.has_value() && summary->owner_id != *filter.owner_id) {
            continue;
        }
        if (filter.kind.has_value() && summary->kind != *filter.kind) {
            continue;
        }
        matches.push_back(std::move(*summary));
    }
    std::ranges::sort(matches, [](const auto& left, const auto& right) {
        return std::tie(right.section, right.transaction_hash) < std::tie(left.section, left.transaction_hash);
    });

    std::size_t offset = 0;
    if (filter.cursor.has_value()) {
        const auto iterator =
            std::ranges::find(matches, *filter.cursor, &ExtraChain::Contracts::ContractSummary::transaction_hash);
        if (iterator != matches.end()) {
            offset = static_cast<std::size_t>(std::distance(matches.begin(), iterator)) + 1;
        }
    }
    const auto limit = std::clamp<std::size_t>(filter.limit, 1, 100);
    const auto end   = std::min(matches.size(), offset + limit);
    page.items.insert(page.items.end(),
                      matches.begin() + static_cast<std::ptrdiff_t>(offset),
                      matches.begin() + static_cast<std::ptrdiff_t>(end));
    if (end < matches.size() && !page.items.empty()) {
        page.next_cursor = page.items.back().transaction_hash;
    }
    return page;
}

bool DagCache::rebuild_contract_catalog() {
    std::lock_guard lock(mutex_);
    if (!init_db() || dag == nullptr || !cache_db_->query("DELETE FROM contract_catalog")) {
        return false;
    }
    const auto first = dag->first_saved_section();
    const auto last  = dag->current_section();
    if (first < SectionId(0) || last < first) {
        return true;
    }
    for (SectionId section_id = first; section_id <= last; ++section_id) {
        const auto section = dag->read_section(section_id);
        if (!section.has_value()) {
            continue;
        }
        for (const auto& transaction : section->transactions) {
            index_contract_transaction(transaction);
        }
    }
    return true;
}
