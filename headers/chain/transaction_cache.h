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
#include "chain/transaction.h"
#include "chain/transaction_status.h"
#include "runtime/event.h"

namespace ExtraChain::Core {
    class ExtraChainNode;
}
class Transaction;

class TransactionCache {
public:
    explicit TransactionCache(ExtraChain::Core::ExtraChainNode *node);
    void make_files();
    void add(const Transaction &transaction);
    void request(ActorId actor_id, TokenId token, bool reward_hidden, std::uint64_t from_time);
    void make_cache();
    ExtraChain::Core::Event<ActorId, TokenId, int, std::vector<TransactionInfo>> &response_event();
    ExtraChain::Core::Event<const Transaction &, StatusTrx::StatusTrxType>       &self_transaction_event();

private:
    void adding(const Transaction &transaction);
    void prepare(ActorId actor_id, TokenId token, bool reward_hidden, std::uint64_t from_time);
    void cache();

    ExtraChain::Core::ExtraChainNode                                            *node;
    ExtraChain::Core::Event<ActorId, TokenId, int, std::vector<TransactionInfo>> response_event_;
    ExtraChain::Core::Event<const Transaction &, StatusTrx::StatusTrxType>       self_transaction_event_;

    friend class Dag;
};
