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

#include "managers/extrachain_node.h"
#include "blockchain/blockchain.h"
#include "utils/db_connector.h"

TransactionCache::TransactionCache(ExtraChainNode *node, QObject *parent)
    : node(node) {
    QDir().mkdir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_CACHE_FOLDER));

    DbConnector db("blockchain/cache/SelfTransactions.db");
    db.open();
    db.create_table(Config::DataStorage::TX_CACHE_CREATE);
    db.close();

    connect(this, &TransactionCache::add, this, &TransactionCache::adding);
    connect(this, &TransactionCache::request, this, &TransactionCache::prepare);
}

void TransactionCache::adding(const BigNumber &block_id, uint64_t block_date, const Transaction &transaction) {
    // TODO: remove
    if (transaction.token() != ActorId("468faf2f1be6504a9a26f7f027f7e43380b0d77d")) {
        return;
    }

    auto map = Utils::to_dbrow(transaction);
    map.erase("prev_block");
    map["block"] = block_id.to_string();
    map["date"]  = block_date;

    DbConnector db("blockchain/cache/SelfTransactions.db");
    db.open();
    bool res = db.insert(Config::DataStorage::TX_CACHE_TABLE, map);
    db.close();

    if (res) {
        node->blockchain()->selfTxAdded(block_id, block_date, transaction);
    }
}

void TransactionCache::prepare(ActorId actor_id, ActorId token, int offset) {
    eLog("[TransactionCache] Prepare for {} with offset {}", actor_id, offset);

    DbConnector db("blockchain/cache/SelfTransactions.db");
    db.open();
    const auto query    = std::format("SELECT * FROM {}", Config::DataStorage::TX_CACHE_TABLE); // TODO: offset
    const auto selected = db.select(query, Config::DataStorage::TX_CACHE_TABLE);
    db.close();

    std::vector<TransactionInfo> transactions;
    for (const auto &map : selected) {
        std::string block_id   = map.at("block");
        std::string block_date = map.at("date");

        auto map2          = map;
        map2["prev_block"] = map.at("block");
        map2.erase("block");
        map2.erase("date");

        auto tx = Utils::from_dbrow<Transaction>(map2);
        if (!tx.has_value()) {
            continue;
        }

        TransactionAmountOperation operation = TransactionAmountOperation::Plus;

        if (actor_id == tx->receiver() && tx->type() == TransactionType::Regular) {
            operation = TransactionAmountOperation::Minus;
        }

        auto transaction_info = TransactionInfo { .block_id    = BigNumber(block_id),
                                                  .block_date  = 1111,
                                                  .operation   = TransactionAmountOperation::Plus,
                                                  .transaction = tx.value() };
    }

    emit this->response(actor_id, token, 0, transactions);
}
