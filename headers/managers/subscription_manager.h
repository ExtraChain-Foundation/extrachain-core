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

#include "chain/actor_id.h"
#include "utils/exc_utils.h"
#include "dfs/dfs_utils.h"

class ExtraChainNode;
class Transaction;

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

    std::expected<Dfs::DirRow, Dfs::DfsError> create_plans(const std::string&                  file_name,
                                                           const std::vector<SubscriptionPlan> plans);

    bool create_subscription_vector(const ActorId&     plan_owner_id,
                                    std::string&       plan_file_id,
                                    const std::string& subscription_name);

    std::optional<std::pair<std::string, std::string>> is_subscription_prepared(
        const ActorId&     owner_id,
        const std::string& subscription_name);

    std::optional<std::unordered_map<std::string, SubscriptionPlan>> read_plans(
        const ActorId&     owner_id,
        const std::string& subscription_name);

private:
    ExtraChainNode* node;

    std::optional<SubscriptionRow> subscription_row;
};
