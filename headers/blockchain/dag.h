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

#include "utils/bignumber.h"
#include "blockchain/transaction.h"

class ExtraChainNode;

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

    std::expected<Transaction, TransactionError> send_transaction(const Transaction       &transaction,
                                                                  const Actor<KeyPrivate> &signer);
    std::expected<void, bool>                    network_transaction(const Transaction &transaction);

    std::unordered_map<ActorId, BigNumberFloat> calculate_actors_balance(const std::vector<ActorId> &actor_ids,
                                                                         const TokenId              &token_id);

private:
    ExtraChainNode *node;
    BigNumber       current_section_     = BigNumber(-1);
    BigNumber       first_saved_section_ = BigNumber(0); // temp, must be -1
    DagMode         mode_                = DagMode::Full;
    DagStatus       status_              = DagStatus::Ready;

    std::optional<std::set<Transaction>> read_transactions(const BigNumber &section);
    std::optional<bool> save_transactions(const BigNumber &section, const std::set<Transaction> &txs);

    bool save_transaction(const Transaction &transaction);

    TransactionProveError prove_transaction(const Transaction &tx, const std::set<Transaction> transactions);
};
