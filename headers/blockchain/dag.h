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

#include <boost/describe.hpp>

#include "network/network_manager.h"
#include "utils/bignumber.h"
#include "blockchain/transaction.h"
#include "blockchain/transaction_cache.h"

class ExtraChainNode;

struct Section {
    BigNumber             id;
    std::uint64_t         timestamp;
    std::set<Transaction> transactions;
};
BOOST_DESCRIBE_STRUCT(Section, (), (timestamp, transactions))

struct TransactionResult {
    std::string           hash;
    TransactionProveError result;
};
BOOST_DESCRIBE_STRUCT(TransactionResult, (), (hash, result))

enum class DagStatus {
    Unknown,
    Started,
    Ready
};

class Dag {
public:
    Dag(ExtraChainNode *node);

    BigNumber current_section() {
        return current_section_;
    }

    DagMode mode() {
        return mode_;
    }

    DagStatus status() {
        return status_;
    }

    void set_mode(DagMode mode) {
        this->mode_ = mode;
    }

    void set_status(DagStatus status) {
        this->status_ = status;
    }

    TransactionCache &transaction_cache() {
        return transaction_cache_;
    }

    std::expected<Transaction, TransactionError> send_transaction(const Transaction       &transaction,
                                                                  const Actor<KeyPrivate> &signer);
    std::expected<void, bool> network_transaction(const Transaction &transaction, const Responder &responder);

    void network_transaction_result(const std::string hash, TransactionProveError result);

    void network_section(const Section &section);

    std::unordered_map<ActorId, BigNumberFloat> calculate_actors_balance(const std::vector<ActorId> &actor_ids,
                                                                         const TokenId              &token_id);

    void add_transaction_sended(const Transaction &transaction);

private:
    ExtraChainNode  *node;
    TransactionCache transaction_cache_;

    BigNumber current_section_     = BigNumber("c");
    BigNumber first_saved_section_ = BigNumber(0); // temp, must be -1
    DagMode   mode_                = DagMode::Full;
    DagStatus status_              = DagStatus::Ready;

    std::unordered_map<std::string, Transaction> sended_transactions;

    std::optional<Section> read_section(const BigNumber &section_id);
    std::optional<bool>    write_section(const Section &section);

    bool save_transaction(const Transaction &transaction);

    TransactionProveError prove_transaction(const Transaction &tx, const std::set<Transaction> transactions);
};
