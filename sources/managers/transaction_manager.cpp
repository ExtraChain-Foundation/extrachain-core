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

#include "managers/transaction_manager.h"

#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "blockchain/blockchain.h"
#include "network/network_manager.h"

std::set<Transaction> TransactionManager::unproved_transactions() const {
    return unproved_transactions_;
}

std::set<Transaction> TransactionManager::proved_transactions() const {
    return proved_transactions_;
}

TransactionManager::TransactionManager(ExtraChainNode *node)
    : /* QObject(node)
    ,*/
    node(node) {
}

void TransactionManager::network_add_transaction(const Transaction &tx) {
    // eLog("[TransactionManager] Added to the waiting list: {}", tx);
    // eLog("addTransactionNetwork {}", node->blockchain()->status());
    if (node->blockchain()->status() != BlockchainStatus::Ready) {
        return;
    }

    unproved_transactions_.insert(tx);
}

void TransactionManager::add_proved_transaction(const Transaction &tx) {
    // eLog("[TransactionManager] Add proved transaction: {}", tx);
    proved_transactions_.insert(tx);
    emit addToCache(tx.receiver().to_string(), tx);
}

// Block making

void TransactionManager::make_block() {
    if (node->accountController()->empty()) {
        eLog("[Blockchain] Account is empty");
        return;
    }

    eInfo("- makeBlock()");

    auto last_block     = node->blockchain()->getLastBlock();

    if (!last_block.has_value()) {
        eLog("[Blockchain] last or real last block is not exists");
        // TODO: request once!
        // node->blockchain()->sync();
        return;
    }

    if (last_block->isEmpty()) {
        eLog("[Blockchain] last or real last block is empty");
        return;
    }

    if (node->blockchain()->status() == BlockchainStatus::Sync) {
        eLog("[Blockchain] Blockchain: try to sync... Last block: {}, type: {}, connections: {}",
             last_block->getIndex(),
             last_block->getType(),
             node->network()->active_connections_count());
        return;
    }

    eLog("[Blockchain] Last block: {}, type: {}, status: {}",
         last_block->getIndex(),
         last_block->getType(),
         node->blockchain()->status());

    if (!node->network()->isActiveConnectionExists()) {
        eLog("[TransactionManager] No active connections");
        return;
    }

    auto maybeGenesisId = last_block->getIndex() + 1;
    if (!last_block->isEmpty() && maybeGenesisId > 0 && Blockchain::isGenesisId(maybeGenesisId)) {
        eLog("[Blockchain] Create genesis block {}, dec: {}",
             maybeGenesisId,
             maybeGenesisId.to_string(NumeralBase::Dec));
        const auto actor   = node->accountController()->mainActor();
        const auto genesis = node->blockchain()->createGenesisBlock(actor);

        if (genesis.has_value() && !genesis->isEmpty()) {
            node->network()->send_message(genesis.value(), MessageType::BlockchainNewBlock, SendMode::Broadcast);
        }

        return;
    }

    if (proved_transactions_.empty()) {
        eLog("[TransactionManager] Try to create block, but pending list is empty");
        return;
    }

    Block block;
    block.setPrev(last_block.value());
    block.addTransactions(proved_transactions_);
    this->proved_transactions_.clear();

    auto blockVariant = BlockVariant(block);
    node->blockchain()->signBlock(blockVariant);
    eLog("[TransactionManager] Send block: {}", blockVariant.getIndex());

    node->network()->send_message(blockVariant, MessageType::BlockchainNewBlock, SendMode::Broadcast);
}

void TransactionManager::timer_block_tick() {
#ifdef IS_R
    auto last_block = node->blockchain()->getLastBlock();
    auto last_id    = last_block.has_value() ? last_block->getIndex() : BigNumber(-1);
    eLog("[Blockchain] Last id: {}. Blockchain status: {}", last_id, node->blockchain()->status());
    unproved_transactions_.clear();
    return;
#endif

    eInfo("[TransactionManager] Make block... {}", node->blockchain()->status());
    make_block();

    if (node->blockchain()->status() == BlockchainStatus::Ready) {
        prove_transactions();
    }
}

void TransactionManager::prove_transactions() {
    auto tx_list = unproved_transactions_;
    unproved_transactions_.clear();
    int index = 0;

    for (const Transaction &tx : std::as_const(tx_list)) {
        eLog("prove_transactions: {} from {}", index, tx_list.size());
        TransactionProveError res = node->blockchain()->prove_transaction(tx, proved_transactions_);

        if (res == TransactionProveError::NoError) {
            eLog("[Blockchain] Transaction approved: {}", tx);
            // eLog("[TransactionManager] Transaction approved!");
            this->add_proved_transaction(tx);
        } else {
            eLog("[Blockchain] Transaction not approved: {} {}", tx, res);
            // eLog("[TransactionManager] Transaction not approved: {}", res);
        }
    }

    // unproved_transactions_.clear();
}

void TransactionManager::process() {
    connect(this, &TransactionManager::network_add_transaction_signal, this, &TransactionManager::network_add_transaction);

    timer_make_block = new QTimer(this);
    connect(timer_make_block, &QTimer::timeout, this, &TransactionManager::timer_block_tick);

    int milliseconds      = QDateTime::currentDateTime().time().msec();
    int seconds           = QDateTime::currentDateTime().time().second();
    int delayToEvenSecond = (seconds % 2 == 0) ? (2000 - milliseconds) : (1000 - milliseconds);
    QThread::msleep(delayToEvenSecond);

    timer_make_block->start(Config::DataStorage::BLOCK_CREATION_PERIOD);
}
