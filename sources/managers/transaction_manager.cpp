/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

#include <QFuture>
#include <QtConcurrent>

#include "managers/extrachain_node.h"

std::set<Transaction> TransactionManager::getReceivedTxList() const {
    return m_receivedTxList;
}

std::set<Transaction> TransactionManager::getPendingTxs() const {
    return m_pendingTxList;
}

TransactionManager::TransactionManager(ExtraChainNode *node)
    : QObject(node)
    , node(node) {
    // setup timer
    blockCreationTimer.setInterval(Config::DataStorage::BLOCK_CREATION_PERIOD);
    connect(&blockCreationTimer, &QTimer::timeout, this,
            &TransactionManager::makeBlockAndProveTransactionsInThread);

    blockCreationTimer.start();
    // prove timer
    //    proveTimer.setInterval(Config::DataStorage::PROVE_TXS_INTERVAL);
    //    connect(&proveTimer, &QTimer::timeout, this, &TransactionManager::proveTransactions);
}

void TransactionManager::removeTransaction(int i) {
    // this->m_pendingTxList.erase(m_pendingTxList.begin() + i);
}

void TransactionManager::addTransaction(const Transaction &tx) {
    // qDebug() << "[TransactionManager] Transaction is being added to the waiting list:" << tx;
    m_receivedTxList.insert(tx);
}

void TransactionManager::addProvedTransaction(const Transaction &tx) {
    // qDebug() << "[TransactionManager] Add proved transaction:" << tx;
    m_pendingTxList.insert(tx);
    emit addToCache(tx.getReceiver().toStdString(), tx);
}

void TransactionManager::runMakeAndProveBlockTimers() {
    qDebug() << "[TransactionManager] Start timers";
    blockCreationTimer.start();
    proveTimer.start();
}

// Block making

void TransactionManager::makeBlock() {
    if (node->accountController()->empty())
        return;

    BlockVariant lastRealBlock = node->blockchain()->getLastRealBlock();
    BlockVariant lastBlock     = node->blockchain()->getLastBlock();

    if (lastRealBlock.getIndex() != lastBlock.getIndex()) {
        qDebug() << "[Blockchain] Last block:" << lastBlock.getIndex() << "| last real:" << lastRealBlock.getIndex() << "|" <<  lastRealBlock.getType();
    } else {
        qDebug() << "[Blockchain] Last block:" << lastRealBlock.getIndex() << "|" << lastRealBlock.getType();
    }

    auto maybeGenesisId = lastBlock.getIndex() + 1;
    if (!lastBlock.isEmpty() && maybeGenesisId > 0 && maybeGenesisId % Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS == 0) {
        qDebug().noquote() << "[Blockchain] Create genesis block" << maybeGenesisId << "| dec:" << maybeGenesisId.toStdString(NumeralBase::Dec);
        const auto   actor = node->accountController()->mainActor();
        GenesisBlock gB    = node->blockchain()->createGenesisBlock(actor);
        node->network()->send_message(gB, MessageType::BlockchainGenesisBlock);
        return;
    }

    if (m_pendingTxList.empty()) {
        lastRealBlock = node->blockchain()->getBlockIndex().getLastRealBlockById();

        if (lastRealBlock.isEmpty()) {
            return;
        }

        if (!node->network()->isActiveConnectionExists()) {
            // qDebug() << "[TransactionManager] Dummy: no active connections";
            if (lastRealBlock.getIndex() != lastBlock.getIndex())
                node->blockchain()->removeAllDummyBlocks(lastBlock);
            return;
        }

        static BigNumber prevDummy = -1;

        if (prevDummy == lastBlock.getIndex() + 1)
            return;

        // creating dummy block in as ordinary block
        Block dummyBlock = Block();
        dummyBlock.setType(BlockType::Dummy);
        dummyBlock.setPrev(lastBlock);
        dummyBlock.addData(lastRealBlock.getIndex().toStdString());
        auto dummyBlockVariant = BlockVariant(dummyBlock);
        node->blockchain()->signBlock(dummyBlockVariant);
        node->network()->send_message(dummyBlockVariant.getBlockConst(), MessageType::BlockchainNewBlock);
        prevDummy = lastBlock.getIndex() + 1;
        return;
    }

    // remove all dummy blocks
    node->blockchain()->removeAllDummyBlocks(lastBlock);

    if (lastRealBlock.isEmpty())
        lastRealBlock = node->blockchain()->getLastRealBlock();
    Block block;
    block.setPrev(lastRealBlock);
    block.addTransactions(m_pendingTxList);
    this->m_pendingTxList.clear();

    auto blockVariant = BlockVariant(block);
    node->blockchain()->signBlock(blockVariant);

    if (blockVariant.isGenesisBlock()) {
        auto genesisBlock = blockVariant.getGenesisBlockConst();
        node->network()->send_message(genesisBlock, MessageType::BlockchainGenesisBlock);
    } else {
        auto dataBlock = blockVariant.getBlockConst();
        node->network()->send_message(dataBlock, MessageType::BlockchainNewBlock);
    }
}

void TransactionManager::makeBlockAndProveTransactionsInThread() {
    makeBlock();
    proveTransactions();
}

void TransactionManager::proveTransactions() {
    for (const Transaction &tx : std::as_const(m_receivedTxList)) {
        TransactionProveError res = node->blockchain()->proveTransaction(tx);

        if (res == TransactionProveError::NoError) {
            qDebug() << "[TransactionManager] Transaction approved:" << tx;
            qDebug() << "[TransactionManager] Transaction approved!";
            this->addProvedTransaction(tx);
        } else {
            qDebug() << "[TransactionManager]" << tx;
            qDebug() << "[TransactionManager] Transaction not approved:" << res;

            // hack
            if (res == TransactionProveError::SelfPleasure && tx.isRewardTransaction()) {
                // node.network()->send_message(tx, MessageType::BlockchainTransaction);
            }
        }
    }

    m_receivedTxList.clear();
}

BigNumberFloat TransactionManager::checkPendingTxsList(const ActorId &sender) {
    BigNumberFloat res = 0;
    if (!m_pendingTxList.empty()) {
        for (const Transaction &tmp : std::as_const(m_pendingTxList)) {
            if (tmp.getSender() == sender) {
                res -= tmp.getAmount();
            } else if (tmp.getReceiver() == sender) {
                res += tmp.getAmount();
            }
        }
    }
    return res;
}
