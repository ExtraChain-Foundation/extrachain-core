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

std::set<Transaction> TransactionManager::getReceivedTxList() const {
    return m_receivedTxList;
}

std::set<Transaction> TransactionManager::getPendingTxs() const {
    return m_pendingTxList;
}

TransactionManager::TransactionManager(ExtraChainNode *node)
    : /* QObject(node)
    ,*/
    node(node) {
}

void TransactionManager::removeTransaction(int i) {
    // this->m_pendingTxList.erase(m_pendingTxList.begin() + i);
}

void TransactionManager::addTransactionNetwork(const Transaction &tx) {
    // eLog("[TransactionManager] Added to the waiting list: {}", tx);
    m_receivedTxList.insert(tx);
}

void TransactionManager::addProvedTransaction(const Transaction &tx) {
    // eLog("[TransactionManager] Add proved transaction: {}", tx);
    m_pendingTxList.insert(tx);
    emit addToCache(tx.receiver().to_string(), tx);
}

// Block making

void TransactionManager::makeBlock() {
    if (node->accountController()->empty())
        return;

    auto lastRealBlock = node->blockchain()->getLastRealBlock();
    auto lastBlock     = node->blockchain()->getLastBlock();

    if (!lastBlock.has_value() || !lastRealBlock.has_value()) {
        eLog("[TransactionManager] last or real last block is not exists");
        // TODO: request once!
        node->blockchain()->sync();
        return;
    }
    if (lastBlock->isEmpty() || lastRealBlock->isEmpty()) {
        eLog("[TransactionManager] last or real last block is empty");
        return;
    }

    if (lastRealBlock->getIndex() != lastBlock->getIndex()) {
        eLog("[Blockchain] Last block: {}, last real: {}, type: {}",
             lastBlock->getIndex(),
             lastRealBlock->getIndex(),
             lastRealBlock->getType());
    } else {
        eLog("[Blockchain] Last block: {}, type: {}", lastRealBlock->getIndex(), lastRealBlock->getType());
    }

    if (!node->network()->isActiveConnectionExists()) {
        // eLog("[TransactionManager] No active connections");

        if (lastRealBlock->getIndex() != lastBlock->getIndex()) {
            node->blockchain()->removeDummyBlocks();
        }
        return;
    }

    auto maybeGenesisId = lastBlock->getIndex() + 1;
    if (!lastBlock->isEmpty() && maybeGenesisId > 0 && Blockchain::isGenesisId(maybeGenesisId)) {
        eLog("[Blockchain] Create genesis block {}, dec: {}",
             maybeGenesisId,
             maybeGenesisId.to_string(NumeralBase::Dec));
        const auto actor   = node->accountController()->mainActor();
        const auto genesis = node->blockchain()->createGenesisBlock(actor);

        if (genesis.has_value() && !genesis->isEmpty()) {
            node->blockchain()->sendBlock(genesis.value());
        }

        return;
    }

    if (m_pendingTxList.empty()) {
        static BigNumber prevDummy = BigNumber(-1);

        // if (prevDummy == lastBlock.getIndex() + 1) {
        //     eLog("[TransactionManager] prevDummy == lastBlock.getIndex() + 1");
        //     return;
        // }

        // creating dummy block in as ordinary block
        // return;
        Block dummyBlock = Block();
        dummyBlock.setType(BlockType::Dummy);
        dummyBlock.setPrev(lastBlock.value());
        dummyBlock.addData(lastRealBlock->getIndex().to_string());
        auto dummyBlockVariant = BlockVariant(dummyBlock);
        node->blockchain()->signBlock(dummyBlockVariant);
        node->blockchain()->sendBlock(dummyBlockVariant);
        prevDummy = lastBlock->getIndex() + 1;
        return;
    }

    Block block;
    block.setPrev(lastRealBlock.value());
    block.addTransactions(m_pendingTxList);
    this->m_pendingTxList.clear();

    auto blockVariant = BlockVariant(block);
    node->blockchain()->signBlock(blockVariant);
    node->blockchain()->sendBlock(blockVariant);
}

void TransactionManager::makeBlockAndProveTransactionsInThread() {
    return;
    makeBlock();
    proveTransactions();
}

void TransactionManager::proveTransactions() {
    for (const Transaction &tx : std::as_const(m_receivedTxList)) {
        TransactionProveError res = node->blockchain()->proveTransaction(tx, m_pendingTxList);

        if (res == TransactionProveError::NoError) {
            eLog("[TransactionManager] Transaction approved: {}", tx);
            // eLog("[TransactionManager] Transaction approved!");
            this->addProvedTransaction(tx);
        } else {
            eLog("[TransactionManager] Transaction not approved: {} {}", tx, res);
            // eLog("[TransactionManager] Transaction not approved: {}", res);
        }
    }

    m_receivedTxList.clear();
}

void TransactionManager::process() {
    connect(this, &TransactionManager::addTransaction, this, &TransactionManager::addTransactionNetwork, Qt::QueuedConnection);

    blockTimer = new QTimer(this);
    connect(blockTimer, &QTimer::timeout, this, &TransactionManager::makeBlockAndProveTransactionsInThread, Qt::QueuedConnection);

    int milliseconds      = QDateTime::currentDateTime().time().msec();
    int seconds           = QDateTime::currentDateTime().time().second();
    int delayToEvenSecond = (seconds % 2 == 0) ? (2000 - milliseconds) : (1000 - milliseconds);
    QThread::msleep(delayToEvenSecond);

    blockTimer->start(Config::DataStorage::BLOCK_CREATION_PERIOD);
}
