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
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

class ExtraChainNode;
class Transaction;

struct SubscriptionRow {
    ActorId     owner_id;
    std::string file_id;

    int           type       = 0; // TODO: use subscription id
    std::uint64_t date_start = 0; // section date
    bool          auto_renew = false;
    std::string   tariff_id;
    BigNumber     section_id;
    std::string   transaction_hash;
};
BOOST_DESCRIBE_STRUCT(SubscriptionRow,
                      (),
                      (type, date_start, /*tariff_id,*/ auto_renew, section_id, transaction_hash))

enum class SubscriptionInterval {
    Day,
    Week,
    Month,
    Year
};

struct SubscriptionTariff {
    std::string                                  id;
    BigNumberFloat                               price;
    TokenId                                      token_id;
    SubscriptionInterval                         interval;
    std::unordered_map<std::string, std::string> name;
    std::unordered_map<std::string, std::string> desc;

    void generate_id() {
        id = Utils::generate_random_hex(6);
    }

    void add_name(const std::string& lang, const std::string& name) {
        this->name.insert({ lang, name });
    }

    void add_desc(const std::string& lang, const std::string& desc) {
        this->desc.insert({ lang, desc });
    }
};
BOOST_DESCRIBE_STRUCT(SubscriptionTariff, (), (id, price, token_id, interval, name, desc))

struct SubscriptionTariffs {
    std::vector<SubscriptionTariff> tariffs;
};
BOOST_DESCRIBE_STRUCT(SubscriptionTariffs, (), (tariffs))

//
class SubscriptionManager {
public:
    SubscriptionManager(ExtraChainNode* node);

    bool add_subscription(const ActorId&     owner_id,
                          const std::string& file_id,
                          int                type,
                          bool               auto_renew,
                          const TokenId&     token_id);

    void self_tx_repeatable_added(const Transaction& transaction);

    bool create_tafiffs_file(const std::string& file_name, const std::vector<SubscriptionTariff> tariffs);
    bool create_subscription_vector(const ActorId&     tariff_owner_id,
                                    std::string&       tariff_file_id,
                                    const std::string& file_name);

    std::optional<std::vector<SubscriptionTariff>> read_tariffs(const ActorId&     owner_id,
                                                                const std::string& file_name);

private:
    ExtraChainNode* node;

    std::optional<SubscriptionRow> subscription_row;
};
