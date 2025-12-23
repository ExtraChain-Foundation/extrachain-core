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

#include "managers/subscription_manager.h"

#include "chain/dag.h"
#include "dfs/dfs_controller.h"

SubscriptionManager::SubscriptionManager(ExtraChainNode* node)
    : node(node) {
}

bool SubscriptionManager::create_subscription_vector(const ActorId&     plan_owner_id,
                                                     std::string&       plan_file_id,
                                                     const std::string& subscription_name) {
    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    auto db = Dfs::Tables::DirsFile::DirsSpace::database();
    if (!db.has_value()) {
        return false;
    }

    auto search_result = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
        db.value(), network_id, Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE, "SubscriptionTemplate");
    if (!search_result.has_value()) {
        return false;
    }

    auto system_actor_id = node->account_controller()->system_actor().id();
    auto sub_res         = node->dfs()->store_vector(system_actor_id,
                                             system_actor_id,
                                             subscription_name,
                                             network_id,
                                             search_result->file_id);
    if (!sub_res.has_value()) {
        return false;
    }

    // save plans file link json
    auto file_link = Dfs::FileLink { .owner_id = plan_owner_id, .file_id = plan_file_id };
    auto json      = Json::serialize(file_link);

    auto res = node->dfs()->store_data_as_file(system_actor_id,
                                               system_actor_id,
                                               ByteArray(json).toBytes(),
                                               Dfs::Basic::TEMPLATE_SUBSCRIPTION,
                                               subscription_name,
                                               Dfs::DataSecurity::Public);

    return true;
}

std::optional<std::pair<std::string, std::string>> SubscriptionManager::is_subscription_prepared(
    const ActorId&     owner_id,
    const std::string& subscription_name) {

    auto db = Dfs::Tables::DirsFile::DirsSpace::database();
    if (!db.has_value()) {
        return std::nullopt;
    }

    auto search_result = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
        db.value(), owner_id, Dfs::Basic::TEMPLATE_VECTOR, subscription_name);
    if (!search_result.has_value()) {
        return std::nullopt;
    }
    if (search_result->state != Dfs::FileState::Ready) {
        return std::nullopt;
    }

    auto search_result_info = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
        db.value(), owner_id, Dfs::Basic::TEMPLATE_SUBSCRIPTION, subscription_name);

    if (!search_result_info.has_value()) {
        return std::nullopt;
    }
    if (search_result_info->state != Dfs::FileState::Ready) {
        return std::nullopt;
    }

    return std::pair { search_result->file_id, search_result_info->file_id };
}

std::optional<std::unordered_map<std::string, SubscriptionPlan>> SubscriptionManager::read_plans(
    const ActorId&     owner_id,
    const std::string& subscription_name) {
    auto file_path = Dfs::Path::file_path(owner_id, "");
    if (!file_path.has_value()) {
        return std::nullopt;
    }

    auto content = Utils::read_file_content(file_path.value());
    if (!content.has_value()) {
        return std::nullopt;
    }

    auto plans = Json::deserialize<std::unordered_map<std::string, SubscriptionPlan>>(content.value());
    if (!plans.has_value()) {
        return std::nullopt;
    }

    return plans.value();
}

bool SubscriptionManager::add_subscription(const ActorId&     owner_id,
                                           const std::string& subscription_name,
                                           const std::string& plan_id) {
    if (subscription_row.has_value()) { // TODO: make map for waiting
        return false;                   // TODO: expected
    }

    auto plans = read_plans(owner_id, subscription_name);
    if (plans->find(plan_id) == plans->end()) {
        return false;
    }

    auto plan = plans->at(plan_id);

    ActorId system_id = node->account_controller()->system_actor().id();

    Transaction transaction;
    transaction.set_type(TransactionType::Repeatable);
    transaction.set_sender(system_id);
    transaction.set_receiver(owner_id);
    transaction.set_amount(BigNumberFloat(plan.price, NumeralBase::Dec)); // TODO: get from plan
#ifdef QT_DEBUG
    transaction.set_amount(BigNumberFloat("1.123", NumeralBase::Dec));
#endif
    transaction.set_token(plan.token_id); // TODO: get token_id from json
    transaction.set_meta(
        Json::serialize(Dfs::FileLink { .owner_id = owner_id, .file_id = subscription_row->file_id }));

    node->send_transaction(transaction, node->account_controller()->system_actor());

    auto row = SubscriptionRow { .owner_id = owner_id, .file_id = subscription_row->file_id, .plan_id = plan_id };
    subscription_row = row;

    return true;
}

void SubscriptionManager::self_tx_repeatable_added(const Transaction& transaction) {
    if (!subscription_row.has_value()) {
        return;
    }

    ActorId system_id = node->account_controller()->system_actor().id();
    if (transaction.sender() != system_id) {
        return;
    }

    auto row = subscription_row.value();

    row.date_start       = transaction.timestamp();
    row.section_id       = transaction.section();
    row.transaction_hash = transaction.hash();

    auto row_map = Utils::to_dbrow(row);

    // temp for old vector
    auto section = row_map["section_id"];
    row_map.erase("section_id");
    row_map.insert({ "block_id", section });

    auto res = node->dfs()->add_vector_row(row.owner_id, row.file_id, row_map, system_id);

    if (res) {
        // Add to global subscriptions list
        add_to_list(row.owner_id, row.file_id);

        emit node->subscriptionAdded(row.owner_id, row.file_id);
    }

    subscription_row.reset();
}

bool SubscriptionManager::create_subscription_template() {
    auto subscription_template = Dfs::CollectionTemplate::create("Subscription")
                                     .value()
                                     .add_fields({ Dfs::Field::String("plan_id").not_null(),
                                                   Dfs::Field::Integer("date_start").not_null(),
                                                   Dfs::Field::String("section_id").not_null(),
                                                   Dfs::Field::String("transaction_hash").not_null() });

    auto system_actor_id = node->account_controller()->system_actor().id();
    auto template_res    = node->dfs()->store_template(system_actor_id, subscription_template);
    if (!template_res.has_value()) {
        eCritical("Can't create subscription template, because {}", template_res.error());
        return false;
    }

    return true;
}

std::expected<Dfs::DirRow, Dfs::DfsError> SubscriptionManager::create_plans(
    const std::string&                  file_name,
    const std::vector<SubscriptionPlan> plans) {

    std::unordered_map<std::string, SubscriptionPlan> plans_map;
    for (const auto& plan : plans) {
        plans_map.insert({ Utils::generate_random_hex(6), plan });
    }

    auto json            = Json::serialize(plans_map);
    auto system_actor_id = node->account_controller()->system_actor().id();
    auto res             = node->dfs()->store_data_as_file(system_actor_id,
                                               system_actor_id,
                                               ByteArray(json).toBytes(),
                                               Dfs::Basic::TEMPLATE_SUBSCRIPTION,
                                               file_name,
                                               Dfs::DataSecurity::Public);
    return res;
}

// Repeat system implementation

std::uint64_t SubscriptionManager::calc_end_date(std::uint64_t        date_start,
                                                  SubscriptionInterval interval) const {
    using namespace SubscriptionConst;
    switch (interval) {
        case SubscriptionInterval::Day:   return date_start + MS_PER_DAY;
        case SubscriptionInterval::Week:  return date_start + 7 * MS_PER_DAY;
        case SubscriptionInterval::Month: return date_start + 30 * MS_PER_DAY;
        case SubscriptionInterval::Year:  return date_start + 365 * MS_PER_DAY;
    }
    return date_start;
}

std::uint64_t SubscriptionManager::time_of_day_ms(std::uint64_t timestamp_ms) const {
    return timestamp_ms % SubscriptionConst::MS_PER_DAY;
}

void SubscriptionManager::find_boundaries_in_history(const BigNumber& current_section) {
    // Search backward until we find both 23:50 and midnight crossings
    // No limit - we will eventually reach the needed time

    std::uint64_t prev_time = 0;
    BigNumber     s         = current_section;

    while (s > BigNumber(0)) {
        auto section = node->dag()->read_section(s);
        if (!section.has_value() || section->middle() == 0) {
            s = s - 1;
            continue;
        }

        auto curr_time = time_of_day_ms(section->middle());

        if (prev_time > 0) {
            // Going backward: if curr >= 23:50 and prev < 23:50, then prev is after midnight
            if (curr_time >= SubscriptionConst::BOUNDARY_TIME_MS && prev_time < SubscriptionConst::BOUNDARY_TIME_MS) {
                if (section_midnight_ == BigNumber(-1)) {
                    section_midnight_ = s + 1; // next section (going forward in time)
                    eLog("[SubscriptionManager] History: midnight at section {}", section_midnight_);
                }
            }

            // Going backward: if curr < 23:50 and prev >= 23:50, then prev crossed 23:50
            if (curr_time < SubscriptionConst::BOUNDARY_TIME_MS && prev_time >= SubscriptionConst::BOUNDARY_TIME_MS) {
                if (section_2350_ == BigNumber(-1) && section_midnight_ != BigNumber(-1)) {
                    section_2350_ = s + 1; // next section (going forward in time)
                    eLog("[SubscriptionManager] History: 23:50 at section {}", section_2350_);
                    break; // Found both
                }
            }
        }

        prev_time = curr_time;
        s         = s - 1;
    }
}

void SubscriptionManager::on_section_finalized(const BigNumber& section_id, std::uint64_t section_avg_time) {
    // Only process after successful sync
    if (node->dag()->status() != DagStatus::Ready) {
        return;
    }

    bool is_light = (node->dag()->mode() == DagMode::Light);

    auto time_of_day  = time_of_day_ms(section_avg_time);
    auto day_start_ms = (section_avg_time / SubscriptionConst::MS_PER_DAY) * SubscriptionConst::MS_PER_DAY;

    // Search within this interval (last 20 sections) for boundary crossings
    constexpr int CONTROL_INTERVAL = 20;
    BigNumber     interval_start   = (section_id > BigNumber(CONTROL_INTERVAL)) ? section_id - CONTROL_INTERVAL + 1 : BigNumber(1);

    std::uint64_t prev_time = 0;
    for (BigNumber s = interval_start; s <= section_id; s = s + 1) {
        auto section = node->dag()->read_section(s);
        if (!section.has_value() || section->middle() == 0) {
            continue;
        }

        auto curr_time = time_of_day_ms(section->middle());

        if (prev_time > 0) {
            // Detect 23:50 crossing
            if (prev_time < SubscriptionConst::BOUNDARY_TIME_MS &&
                curr_time >= SubscriptionConst::BOUNDARY_TIME_MS &&
                section_2350_ == BigNumber(-1)) {
                section_2350_ = s;
                eLog("[SubscriptionManager] Detected 23:50 crossing at section {}", s);
            }

            // Detect midnight crossing
            if (prev_time >= SubscriptionConst::BOUNDARY_TIME_MS &&
                curr_time < SubscriptionConst::BOUNDARY_TIME_MS &&
                section_midnight_ == BigNumber(-1)) {
                section_midnight_ = s;
                eLog("[SubscriptionManager] Detected midnight crossing at section {}", s);
            }
        }

        prev_time = curr_time;
    }

    // Late join fallback: if we're after midnight but haven't found boundaries
    // Only for full nodes (light nodes don't have history)
    if (!is_light && time_of_day < SubscriptionConst::BOUNDARY_TIME_MS &&
        day_start_ms != last_boundary_day_ms_ &&
        (section_2350_ == BigNumber(-1) || section_midnight_ == BigNumber(-1))) {
        find_boundaries_in_history(section_id);
    }

    // Schedule renewals when both boundaries detected
    if (section_2350_ != BigNumber(-1) && section_midnight_ != BigNumber(-1)) {
        if (day_start_ms != last_boundary_day_ms_) {
            // Calculate offset: sections from 23:50 to midnight
            auto sections_offset = section_midnight_ - section_2350_;

            // Target section = midnight + offset (symmetric)
            auto target_section = section_midnight_ + sections_offset;

            // Check if target section already has renewal transactions (after sync)
            bool already_processed = false;
            auto target_sec        = node->dag()->read_section(target_section);
            if (target_sec.has_value()) {
                for (const auto& tx : target_sec->transactions) {
                    if (tx.type() == TransactionType::Repeatable && tx.section() != target_section) {
                        already_processed = true;
                        break;
                    }
                }
            }

            if (already_processed) {
                eLog("[SubscriptionManager] Target section {} already has renewals, skipping", target_section);
            } else {
                // Schedule for when target_section becomes current
                pending_renewals_[target_section] = day_start_ms - SubscriptionConst::MS_PER_DAY;
                eLog("[SubscriptionManager] Scheduled renewals for section {}", target_section);
            }

            last_boundary_day_ms_ = day_start_ms;
        }

        // Reset for next day
        section_2350_     = BigNumber(-1);
        section_midnight_ = BigNumber(-1);
    }

    // Renewals are now processed in process_section_renewals() when section is created
}

SubscriptionStatus SubscriptionManager::check_status(const ActorId&     owner_id,
                                                      const std::string& file_id) const {
    // TODO: read subscription from DFS vector and check date
    // For now, placeholder
    return SubscriptionStatus::NotFound;
}

bool SubscriptionManager::is_active(const ActorId& owner_id, const std::string& file_id) const {
    auto status = check_status(owner_id, file_id);
    return status == SubscriptionStatus::Active || status == SubscriptionStatus::GracePeriod;
}

bool SubscriptionManager::add_to_list(const ActorId& owner_id, const std::string& file_id) {
    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    auto row = node->dfs()->read_file_status(network_id, "Subscriptions", Dfs::Basic::TEMPLATE_VECTOR);
    if (!row.has_value()) {
        return false;
    }

    auto result = node->add_file_id(network_id, row->file_id, owner_id, file_id, 0, FileIdState::None);
    return result.has_value();
}

bool SubscriptionManager::verify_renewal_authorization(const ActorId&        owner_id,
                                                        const std::string&    file_id,
                                                        const TokenId&        token,
                                                        const BigNumberFloat& amount) const {
    // Read subscription row from DFS vector
    auto sub_data = node->dfs()->read_vector_row(owner_id, file_id, node->network_id());
    if (!sub_data.has_value()) {
        return false;
    }

    auto sub_row = Json::deserialize<SubscriptionRow>(Json::serialize(sub_data.value()));
    if (!sub_row.has_value()) {
        return false;
    }

    // Read plans to verify token and amount
    auto network_id = node->actor_index()->network_id();
    auto plans = read_plans(network_id, file_id);
    if (!plans.has_value()) {
        return false;
    }

    auto plan_it = plans->find(sub_row->plan_id);
    if (plan_it == plans->end()) {
        return false;
    }

    auto& plan = plan_it->second;

    // Verify token matches
    if (plan.token_id != token) {
        return false;
    }

    // Verify amount matches
    BigNumberFloat plan_cost(plan.price, NumeralBase::Dec);
    if (plan_cost != amount) {
        return false;
    }

    return true;
}

void SubscriptionManager::process_section_renewals(Section& section) {
    auto it = pending_renewals_.find(section.id);
    if (it == pending_renewals_.end()) {
        return; // Not our target section
    }

    // Check if section already has renewal transactions (duplicates from sync)
    for (const auto& tx : section.transactions) {
        if (tx.type() == TransactionType::Repeatable && tx.section() != section.id) {
            eLog("[SubscriptionManager] Section {} already has renewals, skipping", section.id);
            pending_renewals_.erase(it);
            return;
        }
    }

    auto day_start_ms = it->second;
    auto network_id   = node->actor_index()->network_id();

    eLog("[SubscriptionManager] Processing renewals for section {}, day {}",
         section.id, day_start_ms);

    // Read subscriptions list from DFS vector
    auto subs_row = node->dfs()->read_file_status(network_id, "Subscriptions", Dfs::Basic::TEMPLATE_VECTOR);
    if (!subs_row.has_value()) {
        pending_renewals_.erase(it);
        return;
    }

    auto subs_list = node->dfs()->read_vector_rows(network_id, subs_row->file_id, "");
    if (!subs_list.has_value()) {
        pending_renewals_.erase(it);
        return;
    }

    for (const auto& sub_entry : subs_list.value()) {
        ActorId owner_id;
        std::string file_id;

        if (sub_entry.contains("owner")) {
            owner_id = ActorId(sub_entry.at("owner"));
        }
        if (sub_entry.contains("file_id")) {
            file_id = sub_entry.at("file_id");
        }

        if (owner_id.is_zero() || file_id.empty()) {
            continue;
        }

        // Read subscription data from DFS
        auto sub_data = node->dfs()->read_vector_row(owner_id, file_id, network_id);
        if (!sub_data.has_value()) {
            continue;
        }

        SubscriptionRow sub_row;
        sub_row.owner_id = owner_id;
        sub_row.file_id  = file_id;

        // Parse fields from DbRow
        if (sub_data->contains("plan_id")) {
            sub_row.plan_id = sub_data->at("plan_id");
        }
        if (sub_data->contains("date_start")) {
            sub_row.date_start = std::stoull(sub_data->at("date_start"));
        }
        if (sub_data->contains("section_id")) {
            sub_row.section_id = BigNumber(sub_data->at("section_id"), NumeralBase::Dec);
        } else if (sub_data->contains("block_id")) { // legacy
            sub_row.section_id = BigNumber(sub_data->at("block_id"), NumeralBase::Dec);
        }
        if (sub_data->contains("transaction_hash")) {
            sub_row.transaction_hash = sub_data->at("transaction_hash");
        }

        // Read plan to get interval
        auto plans = read_plans(network_id, file_id);
        if (!plans.has_value()) {
            continue;
        }

        auto plan_it = plans->find(sub_row.plan_id);
        if (plan_it == plans->end()) {
            continue;
        }

        auto& plan = plan_it->second;

        // Check if subscription needs renewal
        auto end_date = calc_end_date(sub_row.date_start, plan.interval);
        if (day_start_ms >= end_date) {
            auto tx = get_renewal_transaction(sub_row, plan);
            if (tx.has_value()) {
                section.transactions.insert(tx.value());
                eLog("[SubscriptionManager] Added renewal tx {} to section {}",
                     sub_row.transaction_hash, section.id);
            }
        }
    }

    pending_renewals_.erase(it);
}

std::optional<Transaction> SubscriptionManager::get_renewal_transaction(const SubscriptionRow&  sub,
                                                                          const SubscriptionPlan& plan) {
    // Validate subscription data
    if (sub.transaction_hash.empty() || sub.section_id == BigNumber(0)) {
        return std::nullopt;
    }

    // Check balance
    BigNumberFloat cost(plan.price, NumeralBase::Dec);
    auto           balances    = node->dag()->calculate_actors_balance({ sub.owner_id });
    auto           balance_key = std::make_pair(sub.owner_id, plan.token_id);

    if (balances.find(balance_key) == balances.end() || balances[balance_key] < cost) {
        eLog("[SubscriptionManager] Insufficient balance for renewal: {}", sub.owner_id);
        return std::nullopt;
    }

    // Find original transaction by hash from subscription row
    auto original_section = node->dag()->read_section(sub.section_id);
    if (!original_section.has_value()) {
        return std::nullopt;
    }

    for (const auto& tx : original_section->transactions) {
        if (tx.hash() == sub.transaction_hash) {
            return tx; // Return copy of original transaction
        }
    }

    return std::nullopt;
}
