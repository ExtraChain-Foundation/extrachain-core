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

QList<Transaction *> TransactionManager::getReceivedTxList() const
{
    return receivedTxList;
}

QList<Transaction> TransactionManager::getPendingTxs() const
{
    return pendingTxs;
}

TransactionManager::TransactionManager(AccountController *accountController, Blockchain *blockchain,
                                       NodeManager *nodeManager)
{
    this->accountController = accountController;
    this->blockchain = blockchain;
    this->nodeManager = nodeManager;

    // setup timer
    blockCreationTimer.setInterval(Config::DataStorage::BLOCK_CREATION_PERIOD);
    connect(&blockCreationTimer, &QTimer::timeout, this, &TransactionManager::makeBlock);
    blockCreationTimer.start();
    qDebug() << "start timer:";
}

void TransactionManager::removeTransaction(int i)
{
    this->pendingTxs.removeAt(i);
}

void TransactionManager::addTransaction(Transaction tx)
{
    qDebug() << "TRANSACTION MANAGER: addTransaction " << tx.toString();

    if (tx.isEmpty())
        return;

    Transaction *trx = new Transaction(tx);
    receivedTxList.append(trx);
    connect(trx, &Transaction::ProveMe, blockchain, &Blockchain::proveTx);
    connect(trx, &Transaction::Approved, this, &TransactionManager::addProvedTransaction);
    connect(trx, &Transaction::NotApproved, this, &TransactionManager::removeUnApprovedTransaction);

    connect(trx, &Transaction::addPendingForFeeTxs, this, &TransactionManager::addPendingForFeeTxs);
    connect(trx, &Transaction::addPendingFeeSenderTxs, this, &TransactionManager::addPendingFeeSenderTxs);
    connect(trx, &Transaction::addPendingFeeApproverTxs, this, &TransactionManager::verifyApproverFeeTx);
    //    connect(&tx, &Transaction::Approved, this,
    //    &TransactionManager::makeBlock);
    emit trx->ProveMe(trx);
    //    qDebug() << "tx_manger.cpp <void TransactionManger::addTransaction> (public "
    //                "function)\n after emit tx.ProveMe() signal to Blockshain";
    //    BigNumber receiverBalance = tx.getReceiverBalance();
    //    BigNumber senderBalance = tx.getSenderBalance();
    //    if (!pendingTxs.contains(tx))
    //    {
    //        pendingTxs.append(tx);
    //    }
    //    //    emit SendProveTransactionRequest(senderBalance, receiverBalance, tx.getHash());`
}

void TransactionManager::addProvedTransaction(Transaction *tx)
{
    qDebug() << "addProvedTransaction";
    if (tx->getData().contains(Fee::UNFEE))
        if (!blockchain->checkHaveUNFreezeTx(tx, Serialization::deserialize(tx->getData()).at(0)))
        {
            receivedTxList.removeOne(tx);
            return;
        }
    if (!pendingTxs.contains(*tx))
        pendingTxs.append(*tx);

    receivedTxList.removeOne(tx);
}

void TransactionManager::removeUnApprovedTransaction(Transaction *tx)
{

    receivedTxList.removeOne(tx);
}

void TransactionManager::addPendingForFeeTxs(Transaction *transaction)
{
    for (const auto i : pendingFeeTxs)
    {
        if (transaction->getHash() == Serialization::deserialize(i->getData())[1])
        {
            if (transaction->getAmount() / 100 * Fee::TRANSACTION_FEE == i->getAmount())
            {
                pendingFeeTxs.removeOne(i);
                transaction->sign(accountController->getCurrentActor());
                emit transaction->Approved(transaction);
            }
            else
            {
                qDebug() << "Transaction fee not approved: amount fee and amount transaction not appropriate";
                emit transaction->NotApproved(transaction);
            }
        }
    }
    pendingForFeeTxs.append(transaction);
}

void TransactionManager::verifyApproverFeeTx(Transaction *tx)
{
    // sender == 0  receive ==actor id
    // IF it's fee transaction
    // WAIT FOR 3 SEC
    QList<QByteArray> tempData = Serialization::deserialize(tx->getData());

    Block block = blockchain->getBlockByHash(tempData[1]);
    if (block.isEmpty())
    {
        qDebug() << "[Check fee] Block is not valid. Invalid fee transaction";
        emit tx->NotApproved(tx);
        return;
    }
    Transaction tempTx = block.getTransactionByHash(tempData[2]);
    if (tempTx.getAmount() / 100 / 100 * Fee::TRANSACTION_FEE != tx->getAmount())
    {
        qDebug() << "[Check fee] amount1 != amount2 Fee";
        emit tx->NotApproved(tx);

        return;
    }
    if (tempTx.isEmpty())
    {
        qDebug() << "[Check fee] Fee transaction is not found in block. Invalid transaction";
        emit tx->NotApproved(tx);
        return;
    }

    if (tempTx.getApprover() == tx->getReceiver())
    {
        qDebug() << "Fee approver transaciton successfull approved";
        tx->sign(accountController->getCurrentActor());
        emit tx->Approved(tx);
        return;
    }
    else
    {
        qDebug() << "Current actor is not tx approver and don't get fee";
        tx->NotApproved(tx);
    }
}

void TransactionManager::addPendingFeeSenderTxs(Transaction *tx)
{
    // sender actor  receiver 0
    QByteArray hashTx = Serialization::deserialize(tx->getData())[1];
    for (const auto &i : pendingForFeeTxs)
    {
        if (i->getHash() == hashTx)
        {
            if (i->getAmount() / 100 * Fee::TRANSACTION_FEE == tx->getAmount())
            {
                qDebug() << i->getHash() << " transaction successfull approved";
                tx->sign(accountController->getCurrentActor());
                i->sign(accountController->getCurrentActor());
                emit i->Approved(i);
                emit tx->Approved(tx);
            }
            else
            {
                qDebug() << "Transaction fee not approved: amount fee and amount transaction not appropriate";
                emit tx->NotApproved(tx);
                emit i->NotApproved(i);
            }
        }
    }
    pendingFeeSenderTxs.append(tx);
}

// Tx hashes (for network)

bool TransactionManager::isUnapproved(const QByteArray &txHash)
{
    return unApprovedTxHashes.contains(txHash);
}

void TransactionManager::removeUnapprovedHash(const QByteArray &txHash)
{
    QMutableListIterator<QByteArray> i(unApprovedTxHashes);
    while (i.hasNext())
    {
        if (i.next() == txHash)
            i.remove();
    }
}

void TransactionManager::addUnapprovedHash(QByteArray txHash)
{
    unApprovedTxHashes.append(txHash);
}

void TransactionManager::addVerifiedTx(Transaction tx)
{
    qDebug() << QString("Adding tx[%1] to pending list").arg(tx.toString());
    pendingTxs.append(tx);
}

// Block making

Block TransactionManager::makeBlock()
{
    int txs = pendingTxs.size();
    //    qDebug() << QString("Attempting to make a block from [%1]
    //    txs)").arg(txs);

    if (txs == 0)
    {
        return Block();
    }

    QByteArray data = convertTxs(pendingTxs);
    Block lastBlock = blockchain->getLastBlock();

    Block block(data, lastBlock);
    // QList<Transaction> x = block.extractTransactions();
    blockchain->signBlock(block);
    qDebug() << "Created block:" << block.getIndex();
    blockchain->addBlock(block);

    // fee section start
    //    QList<Transaction> feeTxs = CoinProcess::blockDataToFeeTxs(pendingTxs, block.getHash(),
    //                                                               accountController->getMainActor()->getId(),
    //                                                               accountController->getActorIndex()->companyId);
    //    for (const auto &i : feeTxs)
    //        nodeManager->createTransaction(i);
    // fee section end
    this->pendingTxs.clear();
    return block;
}

QByteArray TransactionManager::convertTxs(const QList<Transaction> &txs)
{
    QList<QByteArray> l;
    for (const Transaction &tx : txs)
    {
        l << tx.serialize();
    }
    return Serialization::serialize(l, Serialization::TRANSACTION_FIELD_SIZE);
}

BigNumber TransactionManager::checkPendingTxsList(const BigNumber &sender)
{
    BigNumber res = 0;
    if (!pendingTxs.isEmpty())
    {
        for (const Transaction &tmp : pendingTxs)
        {
            if (tmp.getSender() == sender)
            {
                res -= tmp.getAmount();
            }
            else if (tmp.getReceiver() == sender)
            {
                res += tmp.getAmount();
            }
        }
    }
    return res;
}

void TransactionManager::process()
{
}
