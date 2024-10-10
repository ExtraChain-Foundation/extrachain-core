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
#include "managers/account_controller.h"
#include "datastorage/blockchain.h"
#include "network/network_manager.h"

std::set<Transaction> TransactionManager::getReceivedTxList() const {
    return m_receivedTxList;
}

std::set<Transaction> TransactionManager::getPendingTxs() const {
    return m_pendingTxList;
}

TransactionManager::TransactionManager(ExtraChainNode &node)
    : node(node) {
    // setup timer
    blockCreationTimer.setInterval(Config::DataStorage::BLOCK_CREATION_PERIOD);
    connect(
        &blockCreationTimer,
        &QTimer::timeout,
        this,
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
    if (node.accountController()->empty())
        return;

    auto lastRealBlock = node.blockchain()->getLastRealBlock();
    auto lastBlock     = node.blockchain()->getLastBlock();

    if (!lastBlock.has_value() || !lastRealBlock.has_value() || lastBlock->isEmpty() || lastRealBlock->isEmpty()) {
        qDebug() << "[TransactionManager] last or real last block is empty";
        return;
    }

    if (lastRealBlock->getIndex() != lastBlock->getIndex()) {
        qDebug() << "[Blockchain] Last block:" << lastBlock->getIndex()
                 << "| last real:" << lastRealBlock->getIndex() << "|" << lastRealBlock->getType();
    } else {
        qDebug() << "[Blockchain] Last block:" << lastRealBlock->getIndex() << "|" << lastRealBlock->getType();
    }

    auto maybeGenesisId = lastBlock->getIndex() + 1;
    if (!lastBlock->isEmpty() && maybeGenesisId > 0 && Blockchain::isGenesisId(maybeGenesisId)) {
        qDebug().noquote() << "[Blockchain] Create genesis block" << maybeGenesisId
                           << "| dec:" << maybeGenesisId.toStdString(NumeralBase::Dec);
        const auto actor   = node.accountController()->mainActor();
        const auto genesis = node.blockchain()->createGenesisBlock(actor);

        if (genesis.has_value() && !genesis->isEmpty()) {
            node.blockchain()->sendBlock(genesis.value());
        }

        return;
    }

    if (m_pendingTxList.empty()) {
        if (!node.network()->isActiveConnectionExists()) {
            qDebug() << "[TransactionManager] Dummy: no active connections";
            // if (lastRealBlock.getIndex() != lastBlock.getIndex())
            // node.blockchain()->removeAllDummyBlocks(lastBlock);
            return;
        }

        static BigNumber prevDummy = -1;

        // if (prevDummy == lastBlock.getIndex() + 1) {
        //     qDebug() << "[TransactionManager] prevDummy == lastBlock.getIndex() + 1";
        //     return;
        // }

        // creating dummy block in as ordinary block
        Block dummyBlock = Block();
        dummyBlock.setType(BlockType::Dummy);
        dummyBlock.setPrev(lastBlock.value());
        dummyBlock.addData(lastRealBlock->getIndex().toStdString());
        auto dummyBlockVariant = BlockVariant(dummyBlock);
        node.blockchain()->signBlock(dummyBlockVariant);
        node.network()->send_message(dummyBlockVariant.getBlockConst(), MessageType::BlockchainNewBlock);
        prevDummy = lastBlock->getIndex() + 1;
        return;
    }

    // remove all dummy blocks
    node.blockchain()->removeAllDummyBlocks(lastBlock.value());

    if (lastRealBlock->isEmpty())
        lastRealBlock = node.blockchain()->getLastRealBlock();
    Block block;
    block.setPrev(lastRealBlock.value());
    block.addTransactions(m_pendingTxList);
    this->m_pendingTxList.clear();

    auto blockVariant = BlockVariant(block);
    node.blockchain()->signBlock(blockVariant);

    if (blockVariant.isGenesisBlock()) {
        auto genesisBlock = blockVariant.getGenesisBlockConst();
        node.network()->send_message(genesisBlock, MessageType::BlockchainGenesisBlock);
    } else {
        auto dataBlock = blockVariant.getBlockConst();
        node.network()->send_message(dataBlock, MessageType::BlockchainNewBlock);
    }
}

void TransactionManager::makeBlockAndProveTransactionsInThread() {
    makeBlock();
    proveTransactions();
}

void TransactionManager::proveTransactions() {
    for (const Transaction &tx : std::as_const(m_receivedTxList)) {
        TransactionProveError res = node.blockchain()->proveTransaction(tx);

        if (res == TransactionProveError::NoError) {
            qDebug() << "[TransactionManager] Transaction approved:" << tx;
            qDebug() << "[TransactionManager] Transaction approved!";
            this->addProvedTransaction(tx);
        } else {
            qDebug() << "[TransactionManager]" << tx;
            qDebug() << "[TransactionManager] Transaction not approved:" << res;
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
