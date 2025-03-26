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

    std::set<std::string> prev_hashs() {
        std::set<std::string> hashs;
        for (const auto &tx : transactions) {
            hashs.insert_range(tx.prev_hash());
        }
        return hashs;
    }
};
BOOST_DESCRIBE_STRUCT(Section, (), (timestamp, transactions))

struct TransactionResult {
    std::string           hash;
    TransactionProveError result;
};
BOOST_DESCRIBE_STRUCT(TransactionResult, (), (hash, result))

struct SectionRange {
    std::string first;
    std::string last;
};
BOOST_DESCRIBE_STRUCT(SectionRange, (), (first, last))

struct DagCache {
    BigNumber                                                       section   = BigNumber(-1);
    std::uint64_t                                                   timestamp = 0;
    std::unordered_map<std::pair<ActorId, TokenId>, BigNumberFloat> balances;
};
BOOST_DESCRIBE_STRUCT(DagCache, (), (section, timestamp, balances))

enum class BlockchainSyncStatus {
    None,
    LastInfo,
    Blocks
};

enum class DagStatus {
    // Process,
    Started,
    Ready,
    Sync,
    Maybe,
    Timered,
};

struct BlockchainLastInfo {
    BigNumber             last_block_id;
    std::set<std::string> last_hash;
    std::uint64_t         zero_date;
};
BOOST_DESCRIBE_STRUCT(BlockchainLastInfo, (), (last_block_id, last_hash, zero_date))

class Dag {
public:
    Dag(ExtraChainNode *node);

    BigNumber current_section() const {
        return current_section_;
    }

    DagMode mode() const {
        return mode_;
    }

    DagStatus status() const {
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

    std::string file_folder(const BigNumber &section) const;
    std::string file_path(const BigNumber &section) const;

    std::expected<Transaction, TransactionError> prepare_transaction(const Transaction       &transaction,
                                                                     const Actor<KeyPrivate> &signer);
    std::expected<Transaction, TransactionError> send_transaction(const Transaction       &transaction,
                                                                  const Actor<KeyPrivate> &signer);
    std::expected<void, bool> network_transaction(const Transaction &transaction, const Responder &responder);

    void network_transaction_result(const std::string hash, TransactionProveError result);

    void network_section(const Section &section);

    std::unordered_map<ActorId, BigNumberFloat> calculate_actors_balance(const std::vector<ActorId> &actor_ids,
                                                                         const TokenId              &token_id);

    void add_transaction_sended(const Transaction &transaction);

    void update_range();

    std::optional<Transaction> search_transaction(const std::string &hash, int deep = 100) const;

    std::optional<Section> read_section(const BigNumber &section_id) const;

    BigNumber calculate_last_cache_id(const BigNumber &id);

    // sync
    void start_sync();
    void start_check();
    void network_status_sync_request(const Responder &responder);
    void network_status_sync_response(const BlockchainLastInfo &last_info, const Responder &responder);

    void network_request_sections(const BigNumber &from, const BigNumber &to, const Responder &responder);
    void network_request_sections_response(const std::string &compressed, const Responder &responder);

    void set_sync_status(BlockchainSyncStatus status) {
        sync_status_ = status;
        // syncStatusChanged(status);
    }

private:
    ExtraChainNode                              *node;
    TransactionCache                             transaction_cache_;
    std::unordered_map<std::string, Transaction> sended_transactions;
    DagCache                                     dag_cache;

    BigNumber current_section_     = BigNumber(-1);
    BigNumber first_saved_section_ = BigNumber(-1);
    DagMode   mode_                = DagMode::Full;
    DagStatus status_              = DagStatus::Started;

    BlockchainSyncStatus                                sync_status_  = BlockchainSyncStatus::None;
    BlockchainSyncStatus                                check_status_ = BlockchainSyncStatus::None;
    BigNumber                                           sync_last_index;
    int                                                 requests_count = 0;
    std::unordered_map<std::string, BlockchainLastInfo> last_info_;
    QTimer                                             *timer_sync;

    void request_sections(const BigNumber &from, const BigNumber &to, const Responder &responder);
    void send_sync_request();

    std::optional<bool>   write_section(const Section &section);
    bool                  save_transaction(const Transaction &transaction);
    TransactionProveError prove_transaction(const Transaction &tx, const std::set<Transaction> &transactions);
    void                  update_cache();

    friend class ExtraChainNode;
};
