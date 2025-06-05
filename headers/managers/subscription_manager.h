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

class ExtraChainNode;
class Transaction;

struct SubscriptionRow {
    ActorId     owner_id;
    std::string file_id;

    int           type       = 0;
    std::uint64_t date_start = 0; // section date
    bool          auto_renew = false;
    BigNumber     section_id;
    std::string   transaction_hash;
};
BOOST_DESCRIBE_STRUCT(SubscriptionRow, (), (type, date_start, auto_renew, section_id, transaction_hash))

class SubscriptionManager {
public:
    SubscriptionManager(ExtraChainNode* node);

    bool add_subscription(const ActorId&     owner_id,
                          const std::string& file_id,
                          int                type,
                          bool               auto_renew,
                          const TokenId&     token_id);

    void self_tx_repeatable_added(const Transaction& transaction);

    bool create_subscription_vector(const std::string& file_name);

private:
    ExtraChainNode* node;

    std::optional<SubscriptionRow> subscription_row;
};
