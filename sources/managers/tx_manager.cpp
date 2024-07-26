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

QList<Transaction> TransactionManager::getReceivedTxList() const {
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
    receivedTxList.append(tx);
}

void TransactionManager::addProvedTransaction(Transaction tx) {
    qDebug() << "addProvedTransaction";
    if (std::find(pendingTxs.begin(), pendingTxs.end(), tx) != pendingTxs.end() || pendingTxs.empty()) {
        pendingTxs.push_back(tx);
        emit addToCache(tx.getReceiver().toStdString(), tx);
    }

    receivedTxList.removeOne(tx);
}

void TransactionManager::removeUnApprovedTransaction(Transaction tx) {
    receivedTxList.removeOne(tx);
}

bool TransactionManager::isUnapproved(const QByteArray &txHash) {
    return unApprovedTxHashes.contains(txHash);
}

void TransactionManager::removeUnapprovedHash(const QByteArray &txHash) {
    QMutableListIterator<QByteArray> i(unApprovedTxHashes);
    while (i.hasNext()) {
        if (i.next() == txHash)
            i.remove();
    }
}

void TransactionManager::addUnapprovedHash(QByteArray txHash) {
    unApprovedTxHashes.append(txHash);
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
    if(extraChainNode->accountController()->empty())
        return;

    for (FarmingTransactionData &farmingTransactionData : farmingTxs) {
        if(farmingTransactionData.canImproveTx())
            pendingTxs.push_back(farmingTransactionData.transaction);
    }

    extraChainNode->dataMiningManager()->interestAccrual();


    if (pendingTxs.empty()) {
        if(lastRealBlock.isEmpty())
            lastRealBlock = blockchain->getBlockIndex().getLastRealBlockById();
        Block lastRealBlockTemp = blockchain->getBlockIndex().getLastRealBlockById();
        qDebug() << lastRealBlockTemp.getIndex() << lastRealBlockTemp.getType().c_str();
        // creating dummy block in as ordinary block
        Block dummyBlock(lastRealBlockTemp.getIndex().toStdString(), lastBlock);
        dummyBlock.setType(Config::DUMMY_BLOCK_TYPE);
        blockchain->signBlock(dummyBlock);
        const int addedBlock = blockchain->addBlock(dummyBlock);
        if(addedBlock == 0)
           lastBlock = dummyBlock;
        lastBlock = blockchain->getLastBlock();
        return;
    }
    if (lastBlock.isEmpty())
        lastBlock = blockchain->getLastBlock();
    // remove all dummy blocks
    blockchain->removeAllDummyBlocks(lastBlock);

    if (lastRealBlock.isEmpty())
        lastRealBlock = blockchain->getLastRealBlock();
    std::string data = convertTxs(pendingTxs);
    Block block(data, lastRealBlock);
    blockchain->signBlock(block);
    const int addedBlock = blockchain->addBlock(block);
    if (addedBlock == 0) {
        lastBlock = block;
        lastRealBlock = block;

        for (FarmingTransactionData &farmingTransactionData : farmingTxs) {
            if(farmingTransactionData.canImproveTx()) {
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

std::string TransactionManager::convertTxs(const std::vector<Transaction> &txs) {
    std::vector<std::string> l;
    for (const Transaction &tx : txs) {
        l.push_back(tx.serialize());
    }
    return Serialization::serialize(l);
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
