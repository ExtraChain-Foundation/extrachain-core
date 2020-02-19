#include "managers/tx_manager.h"

TransactionManager::TransactionManager(AccountController *accountController, Blockchain *blockchain)
{
    this->accountController = accountController;
    this->blockchain = blockchain;

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
    //    connect(&tx, &Transaction::Approved, this,
    //    &TransactionManager::makeBlock);
    emit trx->ProveMe();
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

void TransactionManager::addProvedTransaction()
{
    QObject *s = QObject::sender();
    Transaction *tx = qobject_cast<Transaction *>(s);

    qDebug() << "addProvedTransaction";

    if (!pendingTxs.contains(*tx))
    {
        pendingTxs.append(*tx);
    }

    receivedTxList.removeOne(tx);
}

void TransactionManager::removeUnApprovedTransaction()
{
    QObject *s = QObject::sender();
    Transaction *tx = qobject_cast<Transaction *>(s);
    receivedTxList.removeOne(tx);
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
    //  block.sign(accountController->getCurrentActor());
    blockchain->signBlock(block);
    qDebug() << "Created block:" << block.getIndex();
    QByteArray blockSerialize = block.serialize();
    emit SendBlock(blockSerialize, Messages::ChainMessage::blockMessage);
    blockchain->addBlock(block);
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
