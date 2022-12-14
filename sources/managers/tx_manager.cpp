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

QList<Transaction *> TransactionManager::getReceivedTxList() const {
    return receivedTxList;
}

QList<Transaction> TransactionManager::getPendingTxs() const {
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
    this->pendingTxs.removeAt(i);
}

void TransactionManager::addTransaction(Transaction tx) {
    qDebug() << "TRANSACTION MANAGER: addTransaction " << tx.toString();

    if (tx.isEmpty())
        return;

    Transaction *trx = new Transaction(tx);
    receivedTxList.append(trx);
}

void TransactionManager::addProvedTransaction(Transaction *tx) {
    qDebug() << "addProvedTransaction";
    if (!pendingTxs.contains(*tx))
        pendingTxs.append(*tx);

    receivedTxList.removeOne(tx);
}

void TransactionManager::removeUnApprovedTransaction(Transaction *tx) {
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
    pendingTxs.append(tx);
}

// Block making

void TransactionManager::makeBlock() {
    //    int txs = pendingTxs.size();
    //    qDebug() << QString("Attempting to make a block from [%1]
    //    txs)").arg(txs);

    Block lastBlock = blockchain->getLastBlock();
    if (pendingTxs.empty()) {
        Block lastRealBlock = blockchain->getBlockIndex().getLastRealBlockById();
        qDebug() << lastRealBlock.getIndex() << lastRealBlock.getType().c_str();
        DummyBlock dummyBlock;
        if (!lastRealBlock.isEmpty()) {
            dummyBlock = DummyBlock(lastBlock, lastRealBlock);
        } else {
            GenesisBlock lastGenesisBlock = blockchain->getBlockIndex().getLastGenesisBlock();
            dummyBlock = DummyBlock(lastBlock, lastGenesisBlock);
        }
        blockchain->signBlock(dummyBlock);
        blockchain->addBlock(dummyBlock);

        return;
    }

         // remove all dummy blocks
    blockchain->removeAllDummyBlocks(lastBlock);
    QByteArray data = convertTxs(pendingTxs);
    lastBlock = blockchain->getLastRealBlock();
    Block block(data, lastBlock);
    // QList<Transaction> x = block.extractTransactions();
    blockchain->signBlock(block);
    blockchain->addBlock(block);

         // fee section start
         //    QList<Transaction> feeTxs = CoinProcess::blockDataToFeeTxs(pendingTxs, block.getHash(),
         //                                                               accountController->getMainActor()->getId(),
         //                                                               accountController->getActorIndex()->m_firstId);
         //    for (const auto &i : feeTxs)
         //        extraChainNode->createTransaction(i);
         // fee section end
    this->pendingTxs.clear();
}

void TransactionManager::proveTransactions() {
    //    const auto dummyBlockCreator = [this]() -> std::unique_ptr<DummyBlock> {
    //        if (pendingTxs.empty()) {
    //            return blockchain->createDummyBlock();
    //        } else {
    //            return std::unique_ptr<DummyBlock> { nullptr };
    //        }
    //    };

    for (auto tx : receivedTxList) {
        blockchain->proveTx(tx);
    }
}

QByteArray TransactionManager::convertTxs(const QList<Transaction> &txs) {
    std::vector<std::string> l;
    for (const Transaction &tx : txs) {
        l.push_back(tx.serialize());
    }
    std::string s = MessagePack::serialize(l);
    return QByteArray::fromStdString(s);
}

BigNumber TransactionManager::checkPendingTxsList(const ActorId &sender) {
    BigNumber res = 0;
    if (!pendingTxs.isEmpty()) {
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

BigNumber TransactionManager::checkRewardTxsList()
{
    BigNumber res = 0;
    if (!pendingTxs.isEmpty()) {
        for (const Transaction &tmp : qAsConst(pendingTxs)) {
            res += tmp.getAmount();
        }
    }
    return res;
}

void TransactionManager::process() {
}
