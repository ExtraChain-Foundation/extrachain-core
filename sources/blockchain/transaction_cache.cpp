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

#include "blockchain/transaction_cache.h"

#include <QDir>

#include "managers/extrachain_node.h"
#include "blockchain/blockchain.h"
#include "utils/db_connector.h"

TransactionCache::TransactionCache(ExtraChainNode *node, QObject *parent)
    : node(node) {
    connect(this, &TransactionCache::add, this, &TransactionCache::adding);
    connect(this, &TransactionCache::request, this, &TransactionCache::prepare);
    connect(this, &TransactionCache::make_cache, this, &TransactionCache::cache);

    make_files();
}

void TransactionCache::make_files() {
    QDir().mkdir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER));
    QDir().mkdir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_CACHE_FOLDER));

    is_exists = QFile(Config::DataStorage::TX_CACHE_CREATE.c_str()).size() != 0;
    if (is_exists) {
        return;
    }

    DbConnector db(BlockchainConst::TRANSACTION_CACHE);
    db.open();
    db.create_table(Config::DataStorage::TX_CACHE_CREATE);
    db.close();
}

void TransactionCache::cache() {
    if (is_exists) {
        return;
    }

    eLog("[TransactionCache] Start first cache");

    auto ids = node->accountController()->accountsIds();
    auto txs = node->blockchain()
                   ->getBlockIndex()
                   .getTxsBySenderOrReceiverInRow(ids,
                                                  BigNumber(-1),
                                                  50,
                                                  ActorId("468faf2f1be6504a9a26f7f027f7e43380b0d77d"));

    for (const auto &[actor_id, tx_infos] : txs) {
        for (const auto &info : tx_infos) {
            adding(info.block_id, info.block_date, info.transaction);
        }
    }

    eLog("[TransactionCache] Finish first cache");
}

void TransactionCache::adding(const BigNumber &block_id, uint64_t block_date, const Transaction &transaction) {
    // TODO: remove
    if (transaction.token() != ActorId("468faf2f1be6504a9a26f7f027f7e43380b0d77d")) {
        return;
    }

    make_files();

    auto map = Utils::to_dbrow(transaction);
    map.erase("prevBlock");
    map["block"] = block_id.to_string();
    map["date"]  = std::to_string(block_date);

    DbConnector db(BlockchainConst::TRANSACTION_CACHE);
    db.open();
    bool res = db.insert(Config::DataStorage::TX_CACHE_TABLE, map);
    db.close();

    if (res) {
        node->blockchain()->selfTxAdded(block_id, block_date, transaction);
    }
}

void TransactionCache::prepare(ActorId actor_id, ActorId token, bool reward_hidden, std::uint64_t from_time) {
    eLog("[TransactionCache] Prepare for {} with from time: {}", actor_id, from_time);

    std::string adding_query;
    if (reward_hidden) {
        adding_query = fmt::format("AND type != '{}'", int(TransactionType::Conversion));
    }

    if (from_time == 0) {
        from_time = std::numeric_limits<std::uint64_t>::max();
    }

    DbConnector db(BlockchainConst::TRANSACTION_CACHE);
    db.open();

    const auto query = fmt::format(
        "SELECT * FROM {} WHERE (sender = '{}' OR receiver = '{}') AND token = '{}' AND date < '{}' {} ORDER by "
        "date DESC LIMIT 50;",
        Config::DataStorage::TX_CACHE_TABLE,
        actor_id.to_string(),
        actor_id.to_string(),
        token.to_string(),
        from_time,
        adding_query);

    const auto selected = db.select(query, Config::DataStorage::TX_CACHE_TABLE);
    db.close();

    std::vector<TransactionInfo> transactions;
    for (const auto &map : selected) {
        std::string block_id   = map.at("block");
        std::string block_date = map.at("date");

        auto map2         = map;
        map2["prevBlock"] = map.at("block");
        map2.erase("block");
        map2.erase("date");

        auto tx = Utils::from_dbrow<Transaction>(map2);
        if (!tx.has_value()) {
            continue;
        }

        TransactionAmountOperation operation = TransactionAmountOperation::Plus;
        if (actor_id == tx->sender()
            && (tx->type() == TransactionType::Regular || tx->type() == TransactionType::Repeatable)) {
            operation = TransactionAmountOperation::Minus;
        }

        auto transaction_info = TransactionInfo { .block_id    = BigNumber(block_id),
                                                  .block_date  = std::stoull(map.at("date")),
                                                  .operation   = operation,
                                                  .transaction = tx.value() };

        transactions.push_back(transaction_info);
    }

    emit this->response(actor_id, token, 0, transactions);
}
