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

#include "chain/transaction_cache.h"

#include "chain/chain_index.h"
#include "chain/dag.h"
#include "core/extrachain_node.h"

TransactionCache::TransactionCache(ExtraChain::Core::ExtraChainNode *node)
    : node(node) {
    make_files();
}

void TransactionCache::add(const Transaction &transaction) {
    adding(transaction);
}

void TransactionCache::request(ActorId actor_id, TokenId token, bool reward_hidden, std::uint64_t from_time) {
    prepare(actor_id, token, reward_hidden, from_time);
}

void TransactionCache::make_cache() {
    cache();
}

ExtraChain::Core::Event<ActorId, TokenId, int, std::vector<TransactionInfo>> &TransactionCache::response_event() {
    return response_event_;
}

ExtraChain::Core::Event<const Transaction &, StatusTrx::StatusTrxType> &TransactionCache::
    self_transaction_event() {
    return self_transaction_event_;
}

void TransactionCache::make_files() {
}

void TransactionCache::cache() {
}

void TransactionCache::adding(const Transaction &transaction) {
    static const auto exc_token = ActorId::create("468faf2f1be6504a9a26f7f027f7e43380b0d77d");
    if (!exc_token.has_value() || transaction.token() != exc_token.value()) {
        return;
    }
    self_transaction_event_.publish(transaction, StatusTrx::StatusTrxType::Approved);
}

void TransactionCache::prepare(ActorId actor_id, ActorId token, bool reward_hidden, std::uint64_t from_time) {
    if (actor_id.is_zero()) {
        return;
    }

    eLog("[TransactionCache] Prepare for {} with from time: {}", actor_id, from_time);

    std::vector<TransactionInfo> transactions;
    auto                        *dag   = node->dag();
    auto                        *index = dag != nullptr ? dag->chain_index() : nullptr;
    if (index == nullptr) {
        response_event_.publish(actor_id, token, 0, std::move(transactions));
        return;
    }

    const auto before = from_time == 0 ? std::numeric_limits<std::uint64_t>::max() : from_time;
    const auto entries =
        index->find_for_actor(actor_id.to_string(), token.to_string(), before, reward_hidden ? 200 : 50);
    std::set<std::string> selected_hashes;
    for (const auto &entry : entries) {
        auto section = dag->read_section(entry.section_id);
        if (!section.has_value()) {
            continue;
        }

        auto matching = std::ranges::find_if(section.value().transactions, [&](const Transaction &transaction) {
            return !selected_hashes.contains(transaction.hash())
                   && transaction.sender().to_string() == entry.sender
                   && transaction.receiver().to_string() == entry.receiver
                   && transaction.token().to_string() == entry.token
                   && static_cast<int>(transaction.type()) == entry.type
                   && transaction.timestamp() == entry.timestamp
                   && transaction.amount().to_string() == entry.amount;
        });
        if (matching == section.value().transactions.end()) {
            continue;
        }
        if (reward_hidden
            && (matching->type() == TransactionType::Conversion || matching->type() == TransactionType::Reward)) {
            continue;
        }

        selected_hashes.insert(matching->hash());
        const auto operation = actor_id == matching->sender()
                                       && (matching->type() == TransactionType::Regular
                                           || matching->type() == TransactionType::Repeatable)
                                   ? TransactionAmountOperation::Minus
                                   : TransactionAmountOperation::Plus;
        transactions.push_back(
            TransactionInfo { .operation = operation, .transaction = *matching, .hash = matching->hash() });
        if (transactions.size() == 50) {
            break;
        }
    }

    response_event_.publish(actor_id, token, 0, std::move(transactions));
}
