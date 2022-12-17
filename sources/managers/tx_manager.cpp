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
    connect(&blockCreationTimer, &QTimer::timeout, this, &TransactionManager::makeBlock);
    blockCreationTimer.start();

    // prove timer
    proveTimer.setInterval(Config::DataStorage::PROVE_TXS_INTERVAL);
    connect(&proveTimer, &QTimer::timeout, this, &TransactionManager::proveTransactions);

    qDebug() << "start timer:";
    proveTimer.start();
}

void TransactionManager::removeTransaction(int i) {
    this->pendingTxs.erase(pendingTxs.begin() + i);
}

void TransactionManager::addTransaction(Transaction tx) {
    qDebug() << "TRANSACTION MANAGER: addTransaction " << tx.toString();

    if (tx.isEmpty())
        return;
    receivedTxList.append(tx);
}

void TransactionManager::addProvedTransaction(Transaction tx) {
    qDebug() << "addProvedTransaction";
    if (std::find(pendingTxs.begin(), pendingTxs.end(), tx) != pendingTxs.end())
        pendingTxs.push_back(tx);

    receivedTxList.removeOne(tx);
}

void TransactionManager::removeUnApprovedTransaction(Transaction tx) {
    receivedTxList.removeOne(tx);
}

// Tx hashes (for network)

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

// Block making

void TransactionManager::makeBlock() {
    qDebug() << "trying makeBlock";
    Block lastBlock = blockchain->getLastBlock();
    if (pendingTxs.empty()) {
        Block lastRealBlock = blockchain->getBlockIndex().getLastRealBlockById();
        qDebug() << lastRealBlock.getIndex() << lastRealBlock.getType().c_str();
        // creating dummy block in as ordinary block
        Block dummyBlock(lastRealBlock.getIndex().toStdString(), lastBlock);
        dummyBlock.setType(Config::DUMMY_BLOCK_TYPE);
        blockchain->signBlock(dummyBlock);
        blockchain->addBlock(dummyBlock);

        return;
    }

    // remove all dummy blocks
    blockchain->removeAllDummyBlocks(lastBlock);
    std::string data = convertTxs(pendingTxs);
    qDebug() << "convertTxs" << data.c_str();
    lastBlock = blockchain->getLastRealBlock();
    Block block(data, lastBlock);
    // QList<Transaction> x = block.extractTransactions();
    blockchain->signBlock(block);
    qDebug() << "Created block:" << block.getIndex() << block.getDigSig().c_str();
    blockchain->addBlock(block);
    this->pendingTxs.clear();
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
        for (const Transaction &tmp : qAsConst(pendingTxs)) {
            if (tmp.getSender() == sender) {
                res -= tmp.getAmount();
            } else if (tmp.getReceiver() == sender) {
                res += tmp.getAmount();
            }
        }
    }
    return res;
}

void TransactionManager::process() {
}
