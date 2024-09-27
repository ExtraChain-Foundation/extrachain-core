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

#include "managers/tx_manager.h"

#include "managers/extrachain_node.h"
#include <QFuture>
#include <QtConcurrent>

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
    qDebug() << "[TransactionManager] Transaction is being added to the waiting list:" << tx;
    m_receivedTxList.insert(tx);
}

void TransactionManager::addProvedTransaction(const Transaction &tx) {
    qDebug() << "addProvedTransaction";
    m_pendingTxList.insert(tx);
    emit addToCache(tx.getReceiver().toStdString(), tx);
}

void TransactionManager::addVerifiedTx(Transaction tx) {
    qDebug() << QString("Adding tx[%1] to pending list").arg(tx.toString());
    m_pendingTxList.insert(tx);
}

void TransactionManager::runMakeAndProveBlockTimers() {
    qDebug() << "start timer:";
    blockCreationTimer.start();
    proveTimer.start();
}

// Block making

void TransactionManager::makeBlock() {
    if (node.accountController()->empty())
        return;

    node.dataMiningManager()->interestAccrual();

    BlockVariant lastRealBlock = node.blockchain()->getLastRealBlock();
    BlockVariant lastBlock = node.blockchain()->getLastBlock();

    if (m_pendingTxList.empty()) {
        // if (lastRealBlock.isEmpty())
            lastRealBlock = node.blockchain()->getBlockIndex().getLastRealBlockById();
        // qDebug() << lastRealBlock.getIndex() << lastRealBlock.getType();
        if (lastRealBlock.isEmpty()) {
            return;
        }

        // creating dummy block in as ordinary block
        Block dummyBlock = Block();
        dummyBlock.setType(BlockType::Dummy);
        dummyBlock.setPrev(lastBlock);
        dummyBlock.addData(lastRealBlock.getIndex().toStdString());
        auto dummyBlockVariant = BlockVariant(dummyBlock);
        node.blockchain()->signBlock(dummyBlockVariant);
        const int addedBlock = node.blockchain()->addBlock(dummyBlockVariant);
        if (addedBlock == 0)
            lastBlock = dummyBlockVariant;
        lastBlock = node.blockchain()->getLastBlock();
        return;
    }

    // remove all dummy blocks
    node.blockchain()->removeAllDummyBlocks(lastBlock);

    if (lastRealBlock.isEmpty())
        lastRealBlock = node.blockchain()->getLastRealBlock();
    Block block;
    block.setPrev(lastRealBlock);
    block.addTransactions(m_pendingTxList);

    auto blockVariant = BlockVariant(block);
    node.blockchain()->signBlock(blockVariant);
    const int addedBlock = node.blockchain()->addBlock(blockVariant);

    if (addedBlock == 0) {
        lastBlock = blockVariant;
        lastRealBlock = blockVariant;
    }

    this->m_pendingTxList.clear();
}

void TransactionManager::makeBlockAndProveTransactionsInThread() {
    makeBlock();
    proveTransactions();
}

void TransactionManager::proveTransactions() {
    for (const Transaction &tx : m_receivedTxList) {
        TransactionProveError res = node.blockchain()->proveTx(tx);

        if (res == TransactionProveError::NoError) {
            qDebug() << "Transaction approved!";
            qDebug() << tx;
        } else {
            qDebug() << "Transaction not approved:" << res;
            qDebug() << tx;
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
