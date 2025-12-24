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

#include "boost/describe.hpp"

#include "chain/actor_id.h"
#include "utils/exc_utils.h"
#include "dfs/dfs_utils.h"

class ExtraChainNode;
class Transaction;
struct Section;

namespace SubscriptionConst {
    constexpr std::uint32_t GRACE_PERIOD_DAYS = 3;
    constexpr std::uint64_t MS_PER_DAY        = 86400000ULL;
    constexpr std::uint64_t BOUNDARY_TIME_MS  = 23ULL * 60 * 60 * 1000 + 50 * 60 * 1000; // 23:50 UTC
}

enum class SubscriptionStatus { Active, GracePeriod, Expired, NotFound };
enum class RenewalResult { Success, InsufficientBalance, PlanNotFound, TransactionFailed, SubscriptionNotFound };

struct SubscriptionRow {
    ActorId     owner_id;
    std::string file_id;

    std::string   plan_id;
    std::uint64_t date_start = 0;
    BigNumber     section_id;
    std::string   transaction_hash;
};
BOOST_DESCRIBE_STRUCT(SubscriptionRow, (), (plan_id, date_start, section_id, transaction_hash))

enum class SubscriptionInterval {
    Day,
    Week,
    Month,
    Year
};

struct SubscriptionPlan {
    std::string                                  price; // BigNumberFloat
    TokenId                                      token_id;
    SubscriptionInterval                         interval;
    std::unordered_map<std::string, std::string> name;
    // std::unordered_map<std::string, std::string> desc;

    void add_name(const std::string& lang, const std::string& name) {
        this->name.insert({ lang, name });
    }

    // void add_desc(const std::string& lang, const std::string& desc) {
    //     this->desc.insert({ lang, desc });
    // }
};
BOOST_DESCRIBE_STRUCT(SubscriptionPlan, (), (price, token_id, interval, name))

//
class SubscriptionManager {
public:
    SubscriptionManager(ExtraChainNode* node);

    bool add_subscription(const ActorId&     owner_id,
                          const std::string& subscription_name,
                          const std::string& plan_id);

    void self_tx_repeatable_added(const Transaction& transaction);

    bool create_subscription_template();

    bool create_subscription_vector(const std::string&                 subscription_name,
                                    const std::vector<SubscriptionPlan>& plans);

    std::optional<std::string> is_subscription_prepared(const ActorId&     owner_id,
                                                        const std::string& subscription_name);

    std::optional<std::unordered_map<std::string, SubscriptionPlan>> read_plans(
        const ActorId&     owner_id,
        const std::string& subscription_name) const;

    // Repeat system
    void on_section_finalized(const BigNumber& section_id, std::uint64_t section_avg_time);
    void process_section_renewals(Section& section);
    bool add_to_list(const ActorId& owner_id, const std::string& file_id);
    SubscriptionStatus check_status(const ActorId& owner_id, const std::string& file_id) const;
    bool               is_active(const ActorId& owner_id, const std::string& file_id) const;

    bool verify_renewal_authorization(const ActorId&        owner_id,
                                      const std::string&    file_id,
                                      const TokenId&        token,
                                      const BigNumberFloat& amount) const;

private:
    std::uint64_t calc_end_date(std::uint64_t date_start, SubscriptionInterval interval) const;
    std::uint64_t time_of_day_ms(std::uint64_t timestamp_ms) const;
    std::optional<Transaction> get_renewal_transaction(const SubscriptionRow& sub, const SubscriptionPlan& plan);

    // Find boundary sections in recent history (for late join)
    void find_boundaries_in_history(const BigNumber& current_section);

    ExtraChainNode* node;

    std::optional<SubscriptionRow> subscription_row;
    std::uint64_t                  last_boundary_day_ms_ = 0;
    BigNumber                      section_2350_         = BigNumber(-1); // first section after 23:50
    BigNumber                      section_midnight_     = BigNumber(-1); // first section after 00:00

    // Pending renewals: target_section -> day_start_ms
    std::map<BigNumber, std::uint64_t> pending_renewals_;
};
