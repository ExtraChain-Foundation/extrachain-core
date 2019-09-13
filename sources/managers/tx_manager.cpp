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

int TransactionManager::addTransaction(Transaction tx)
{
    qDebug() << "TRANSACTION MANAGER: addTransaction " << tx.toString();
    if (tx.isEmpty())
    {
        return Errors::TRANSACTION_IS_EMPTY;
    }

    receivedTxList.append(tx);
    connect(&tx, &Transaction::ProveMe, blockchain, &Blockchain::proveTx);
    //    connect(&tx, &Transaction::Approved, this,
    //    &TransactionManager::makeBlock);
    emit tx.ProveMe();
    qDebug() << "tx_manger.cpp <void TransactionManger::addTransaction> (public "
                "function)\n after emit tx.ProveMe() signal to Blockshain";
    BigNumber receiverBalance = tx.getReceiverBalance();
    BigNumber senderBalance = tx.getSenderBalance();
    if (!pendingTxs.contains(tx))
    {
        pendingTxs.push_back(tx);
    }
    emit SendProveTransactionRequest(senderBalance, receiverBalance, tx.getHash());
    return 0;
}

int TransactionManager::proveTransaction(BigNumber senderId, BigNumber receiverId, Transaction sender,
                                         Transaction receiver, QByteArray txHash)
{
    qDebug() << "tx_manger ProveTransaction() << function begin {";
    Transaction transaction;
    for (const Transaction &tx : pendingTxs)
    {
        if (tx.getHash() == txHash)
        {
            transaction = tx;
            break;
        }
    }

    // DELETEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE
    if (accountController->getCurrentActor().getId() == senderId)
    {
        this->pendingTxs.push_back(transaction);
        return 0;
    }
    // DELETEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE

    if (senderId == BigNumber("0"))
    {
        this->pendingTxs.push_back(transaction);
        return 0;
    }
    BigNumber receiverBalance = transaction.getReceiverBalance();
    BigNumber senderBalance = transaction.getSenderBalance();

    BigNumber lastReceiverBalance = sender.getReceiverBalance();
    BigNumber lastSenderBalance = receiver.getSenderBalance();

    if (receiverBalance != lastReceiverBalance)
    {
        qDebug() << " Can't add transaction" << transaction.toString() << ": receiver balance "
                 << receiverBalance << "is not equal to last saved value" << lastReceiverBalance;
        return Errors::TRANSACTION_WRONG_RECEIVER_BALANCE;
    }
    if (senderBalance != lastSenderBalance)
    {
        qDebug() << " Can't add transaction" << transaction.toString() << ": sender balance " << senderBalance
                 << "is not equal to last saved value" << lastSenderBalance;
        return Errors::TRANSACTION_WRONG_SENDER_BALANCE;
    }

    this->pendingTxs.push_back(transaction);
    qDebug() << "tx_manger ProveTransaction() << the transaction have been added "
                "to the "
                "lis << function end }";

    return 0;
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

void TransactionManager::makeBlock()
{
    int txs = pendingTxs.size();
    //    qDebug() << QString("Attempting to make a block from [%1]
    //    txs)").arg(txs);

    if (txs == 0)
    {
        return;
    }

    QByteArray data = convertTxs(pendingTxs);
    qDebug() << data;
    Block lastBlock = blockchain->getLastBlock();

    Block block(data, &lastBlock);
    blockchain->signBlock(block); // Non-approved code

    qDebug() << QString("Created block: [%1]").arg(block.toString());
    QByteArray qw = block.serialize();
    qDebug() << qw;
    emit SendBlock(qw);

    this->pendingTxs.clear();
}

QByteArray TransactionManager::convertTxs(const QList<Transaction> &txs)
{
    QList<QByteArray> l;
    for (const Transaction &tx : txs)
    {
        l << tx.serialize();
    }
    return Serialization::universalSerialize(l, Serialization::DEFAULT_FIELD_SIZE);
}

// Thread management //

void TransactionManager::run()
{
    active = true;
    exec();
}

int TransactionManager::exec()
{
    while (isActive())
    {
        //
    }
    return 0;
}

void TransactionManager::quit()
{
    active = false;
}

bool TransactionManager::isActive() const
{
    return this->active;
}
