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

std::vector<Transaction> TransactionManager::getReceivedTxList() const {
    return receivedTxList;
}

std::vector<Transaction> &TransactionManager::getReceivedTxListByReference() {
    return receivedTxList;
}

std::vector<Transaction> TransactionManager::getPendingTxs() const {
    return pendingTxs;
}

TransactionManager::TransactionManager(AccountController *accountController, Blockchain *blockchain,
                                       ExtraChainNode *extraChainNode) {
    this->accountController = accountController;
    this->blockchain = blockchain;
    this->extraChainNode = extraChainNode;

    // setup timer
    blockCreationTimer.setInterval(Config::DataStorage::BLOCK_CREATION_PERIOD);
    connect(&blockCreationTimer, &QTimer::timeout, this,
            &TransactionManager::makeBlockAndProveTransactionsInThread);

    farmingTxs = blockchain->getFarmingTxs();
    blockCreationTimer.start();
    // prove timer
    //    proveTimer.setInterval(Config::DataStorage::PROVE_TXS_INTERVAL);
    //    connect(&proveTimer, &QTimer::timeout, this, &TransactionManager::proveTransactions);
}

void TransactionManager::removeTransaction(int i) {
    this->pendingTxs.erase(pendingTxs.begin() + i);
}

void TransactionManager::addTransaction(Transaction tx) {
    qDebug() << QString("TRANSACTION MANAGER: addTransaction [%1]").arg(tx.toString());

    //    if (tx.isEmpty())
    //        return;
    receivedTxList.push_back(tx);
}

void TransactionManager::addProvedTransaction(Transaction tx) {
    qDebug() << "addProvedTransaction";
    if (std::find(pendingTxs.begin(), pendingTxs.end(), tx) != pendingTxs.end() || pendingTxs.empty()) {
        pendingTxs.push_back(tx);
        emit addToCache(tx.getReceiver().toStdString(), tx);
    }

    // receivedTxList.removeOne(tx);
    auto it = std::remove(receivedTxList.begin(), receivedTxList.end(), tx);
    receivedTxList.erase(it, receivedTxList.end());
}

void TransactionManager::removeUnApprovedTransaction(Transaction tx) {
    // receivedTxList.removeOne(tx);
    auto it = std::remove(receivedTxList.begin(), receivedTxList.end(), tx);
    receivedTxList.erase(it, receivedTxList.end());
}

bool TransactionManager::isUnapproved(const QByteArray &txHash) {
    return Utils::vector_contains(unApprovedTxHashes, txHash);
}

void TransactionManager::removeUnapprovedHash(const QByteArray &txHash) {
    // QMutableListIterator<QByteArray> i(unApprovedTxHashes);
    // while (i.hasNext()) {
    //     if (i.next() == txHash)
    //         i.remove();
    // }
    unApprovedTxHashes.erase(
        std::remove(unApprovedTxHashes.begin(), unApprovedTxHashes.end(), txHash),
        unApprovedTxHashes.end());
}

void TransactionManager::addUnapprovedHash(QByteArray txHash) {
    unApprovedTxHashes.push_back(txHash);
}

void TransactionManager::addVerifiedTx(Transaction tx) {
    qDebug() << QString("Adding tx[%1] to pending list").arg(tx.toString());
    pendingTxs.push_back(tx);
}

void TransactionManager::runMakeAndProveBlockTimers() {
    qDebug() << "start timer:";
    blockCreationTimer.start();
    proveTimer.start();
}

// Block making

void TransactionManager::makeBlock() {
    if (extraChainNode->accountController()->empty())
        return;

    for (FarmingTransactionData &farmingTransactionData : farmingTxs) {
        if (farmingTransactionData.canImproveTx())
            pendingTxs.push_back(farmingTransactionData.transaction);
    }

    extraChainNode->dataMiningManager()->interestAccrual();

    BlockVariant lastRealBlock = blockchain->getLastRealBlock();
    BlockVariant lastBlock = blockchain->getLastBlock();

    if (pendingTxs.empty()) {
        if (lastRealBlock.isEmpty())
            lastRealBlock = blockchain->getBlockIndex().getLastRealBlockById();
        BlockVariant lastRealBlockTemp = blockchain->getBlockIndex().getLastRealBlockById();
        qDebug() << lastRealBlockTemp.getIndex() << lastRealBlockTemp.getType();
        // creating dummy block in as ordinary block
        Block dummyBlock = Block();
        dummyBlock.setType(BlockType::Dummy);
        dummyBlock.setPrev(lastBlock);
        dummyBlock.addData(lastRealBlockTemp.getIndex().toStdString());
        auto dummyBlockVariant = BlockVariant(dummyBlock);
        blockchain->signBlock(dummyBlockVariant);
        const int addedBlock = blockchain->addBlock(dummyBlockVariant);
        if (addedBlock == 0)
            lastBlock = dummyBlockVariant;
        lastBlock = blockchain->getLastBlock();
        return;
    }

    // remove all dummy blocks
    blockchain->removeAllDummyBlocks(lastBlock);

    if (lastRealBlock.isEmpty())
        lastRealBlock = blockchain->getLastRealBlock();
    Block block;
    block.setPrev(lastRealBlock);
    block.addTransactions(pendingTxs);

    auto blockVariant = BlockVariant(block);
    blockchain->signBlock(blockVariant);
    const int addedBlock = blockchain->addBlock(blockVariant);
    if (addedBlock == 0) {
        lastBlock = blockVariant;
        lastRealBlock = blockVariant;

        for (FarmingTransactionData &farmingTransactionData : farmingTxs) {
            continue; // farming not farm
            if (farmingTransactionData.canImproveTx()) {
                farmingTxs.pop_front();
                continue;
            }
            farmingTransactionData.decrementIndex();
        }
    }
    this->pendingTxs.clear();
}

void TransactionManager::makeBlockAndProveTransactionsInThread() {
    makeBlock();
    proveTransactions();
}

void TransactionManager::proveTransactions() {
    for (auto tx : receivedTxList) {
        blockchain->proveTx(tx);
    }
}

BigNumberFloat TransactionManager::checkPendingTxsList(const ActorId &sender) {
    BigNumberFloat res = 0;
    if (!pendingTxs.empty()) {
        for (const Transaction &tmp : std::as_const(pendingTxs)) {
            if (tmp.getSender() == sender) {
                res -= tmp.getAmount();
            } else if (tmp.getReceiver() == sender) {
                res += tmp.getAmount();
            }
        }
    }
    return res;
}
