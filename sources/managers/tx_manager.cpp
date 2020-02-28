#include "managers/tx_manager.h"

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
        if (transaction->getHash() == Serialization::universalDeserialize(i->getData())[1])
        {
            if (transaction->getAmount() / 100 * Fee::TRANSACTION_FEE == i->getAmount())
            {
                pendingFeeTxs.removeOne(i);
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
    QList<QByteArray> tempData = Serialization::universalDeserialize(tx->getData());

    Block block = blockchain->getBlockByHash(tempData[1]);
    if (block.isEmpty())
    {
        qDebug() << "[Check fee] Block is not valid. Invalid fee transaction";
        emit tx->NotApproved(tx);
        return;
    }

    if (block.getTransactionByHash(tempData[2]).isEmpty())
    {
        qDebug() << "[Check fee] Fee transaction is not found in block. Invalid transaction";
        emit tx->NotApproved(tx);
        return;
    }
    if (block.isApprover(tx->getReceiver().toByteArray()))
    {
        qDebug() << "Fee approver transaciton successfull approved";
        emit tx->Approved(tx);
        return;
    }
    qDebug() << "Undefined behaviour in addPendingFeeApproverTxs";
    tx->NotApproved(tx);
}

void TransactionManager::addPendingFeeSenderTxs(Transaction *tx)
{
    // sender actor  receiver 0
    QByteArray hashTx = Serialization::universalDeserialize(tx->getData())[1];
    for (const auto i : pendingForFeeTxs)
    {

        if (i->getHash() == hashTx)
        {
            if (i->getAmount() / 100 * Fee::TRANSACTION_FEE == tx->getAmount())
            {
                qDebug() << i->getHash() << " transaction successfull approved";
                emit i->Approved(i);
                delete tx;
            }
            else
            {
                qDebug() << "Transaction fee not approved: amount fee and amount transaction not appropriate";
                delete tx;
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

    blockchain->signBlock(block);
    qDebug() << "Created block:" << block.getIndex();
    QByteArray blockSerialize = block.serialize();
    blockchain->addBlock(block);
    this->pendingTxs.clear();

    // fee section start
    QList<Transaction> feeTxs = CoinProcess::blockDataToFeeTxs(pendingTxs, block.getHash(),
                                                               accountController->getMainActor()->getId());
    for (const auto &i : feeTxs)
        nodeManager->createTransaction(i);
    // fee section end
    return block;
}

QByteArray TransactionManager::convertTxs(const QList<Transaction> &txs)
{
    QList<QByteArray> l;
    for (const Transaction &tx : txs)
    {
        l << tx.serialize();
    }
    return Serialization::universalSerialize(l, Serialization::TRANSACTION_FIELD_SIZE);
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
