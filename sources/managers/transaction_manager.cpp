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

TransactionManager::TransactionManager(ExtraChainNode *node)
    : /* QObject(node)
    ,*/
    node(node) {
}

void TransactionManager::removeTransaction(int i) {
    // this->m_pendingTxList.erase(m_pendingTxList.begin() + i);
}

void TransactionManager::addTransactionNetwork(const Transaction &tx) {
    // qDebug() << "[TransactionManager] Added to the waiting list:" << tx;
    m_receivedTxList.insert(tx);
}

void TransactionManager::addProvedTransaction(const Transaction &tx) {
    // qDebug() << "[TransactionManager] Add proved transaction:" << tx;
    m_pendingTxList.insert(tx);
    emit addToCache(tx.receiver().toStdString(), tx);
}

// Block making

void TransactionManager::makeBlock() {
    if (node->accountController()->empty())
        return;

    auto lastRealBlock = node->blockchain()->getLastRealBlock();
    auto lastBlock     = node->blockchain()->getLastBlock();

    if (!lastBlock.has_value() || !lastRealBlock.has_value()) {
        qDebug() << "[TransactionManager] last or real last block is not exists";
        node->blockchain()->sync();
        return;
    }
    if (lastBlock->isEmpty() || lastRealBlock->isEmpty()) {
        qDebug() << "[TransactionManager] last or real last block is empty";
        return;
    }

    if (lastRealBlock->getIndex() != lastBlock->getIndex()) {
        qDebug() << "[Blockchain] Last block:" << lastBlock->getIndex()
                 << "| last real:" << lastRealBlock->getIndex() << "|" << lastRealBlock->getType();
    } else {
        qDebug() << "[Blockchain] Last block:" << lastRealBlock->getIndex() << "|"
                 << lastRealBlock->getType();
    }

    if (!node->network()->isActiveConnectionExists()) {
        // qDebug() << "[TransactionManager] No active connections";

        if (lastRealBlock->getIndex() != lastBlock->getIndex()) {
            node->blockchain()->removeDummyBlocks();
        }
        return;
    }

    auto maybeGenesisId = lastBlock->getIndex() + 1;
    if (!lastBlock->isEmpty() && maybeGenesisId > 0 && Blockchain::isGenesisId(maybeGenesisId)) {
        qDebug().noquote() << "[Blockchain] Create genesis block" << maybeGenesisId
                           << "| dec:" << maybeGenesisId.toStdString(NumeralBase::Dec);
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
        //     qDebug() << "[TransactionManager] prevDummy == lastBlock.getIndex() + 1";
        //     return;
        // }

        // creating dummy block in as ordinary block
        // return;
        Block dummyBlock = Block();
        dummyBlock.setType(BlockType::Dummy);
        dummyBlock.setPrev(lastBlock.value());
        dummyBlock.addData(lastRealBlock->getIndex().toStdString());
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
    makeBlock();
    proveTransactions();
}

void TransactionManager::proveTransactions() {
    for (const Transaction &tx : std::as_const(m_receivedTxList)) {
        TransactionProveError res = node->blockchain()->proveTransaction(tx, m_pendingTxList);

        if (res == TransactionProveError::NoError) {
            qDebug() << "[TransactionManager] Transaction approved:" << tx;
            // qDebug() << "[TransactionManager] Transaction approved!";
            this->addProvedTransaction(tx);
        } else {
            qDebug() << "[TransactionManager] Transaction not approved:" << tx << res;
            // qDebug() << "[TransactionManager] Transaction not approved:" << res;
        }
    }

    m_receivedTxList.clear();
}

void TransactionManager::process() {
    connect(this, &TransactionManager::addTransaction, this, &TransactionManager::addTransactionNetwork);

    blockTimer = new QTimer(this);
    connect(blockTimer, &QTimer::timeout, this, &TransactionManager::makeBlockAndProveTransactionsInThread);

    int milliseconds      = QDateTime::currentDateTime().time().msec();
    int seconds           = QDateTime::currentDateTime().time().second();
    int delayToEvenSecond = (seconds % 2 == 0) ? (2000 - milliseconds) : (1000 - milliseconds);
    QThread::msleep(delayToEvenSecond);

    blockTimer->start(Config::DataStorage::BLOCK_CREATION_PERIOD);
}
