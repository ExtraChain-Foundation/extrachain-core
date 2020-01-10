#ifndef TX_MANAGER_H
#define TX_MANAGER_H

#include <QList>
#include <QObject>
#include <QThread>
#include <QByteArray>
#include <QDebug>
#include <QTimer>

#include "datastorage/blockchain.h"
#include "enc/crypt_interface.h"
#include "datastorage/block.h"
#include "datastorage/transaction.h"
#include "datastorage/index/blockindex.h"
#include "headers/network/packages/service/message_types.h"
/**
 * @brief Process all incoming transactions
 * Approves and packs them into a new block
 */
class TransactionManager : public QObject
{
    Q_OBJECT

private:
    // to create block's from pending txs
    QTimer blockCreationTimer;

    // received transactions that will be packed into block
    QList<Transaction> pendingTxs;

    // (This a network state more)
    // hashes of sent transactions, that are not approved yet
    QList<QByteArray> unApprovedTxHashes;

    QList<Transaction *> receivedTxList;

    // current user
    //    Actor<KeyPrivate> currentUser;
    AccountController *accountController;

    Blockchain *blockchain;
    // received transactions that we need to compare between network and blockchain

public:
    // todo: add ref to blockchain
    TransactionManager(AccountController *accountController, Blockchain *blockchain);

private:
    void removeTransaction(int i);

public:
    static QByteArray convertTxs(const QList<Transaction> &txs);
    BigNumber checkPendingTxsList(const BigNumber &sender);
public slots:
    /**
     * Serialize all transactions to a serialized data.
     * Creates a memblock, and setup data field with serialized data.
     * Emits SendBlock signal.
     */
    Block makeBlock();

    /**
     * If Transaction is valid, adds it to the txList.
     * @param tx - transaction
     * @return 0 is transaction is successfully added
     */
    void addTransaction(Transaction tx);
    void addProvedTransaction();
    void removeUnApprovedTransaction();

    // Unapproved tx's //

    /**
     * @brief isUnaproved
     * @param txHash
     * @return true if there is txHash in unApprovedTxHashes, false otherwise
     */
    bool isUnapproved(const QByteArray &txHash);

    /**
     * @brief Removes hash from unApprovedTxHashes list
     * @param txHash to remove
     */
    void removeUnapprovedHash(const QByteArray &txHash);

    /**
     * @brief addUnapprovedHash
     * @param txHash
     */
    void addUnapprovedHash(QByteArray txHash);

    /**
     * @brief Adds transaction to pending list
     * @param tx - already verified transaction
     */
    void addVerifiedTx(Transaction tx);
    void process();

signals:
    /**
     * @brief Signal to blockchain. We need to enshure, that this is really new tx.
     * (By checking tx existanse in previous blocks)
     * @param tx - transaction to check
     */
    void VerifyTx(Transaction tx);

    /**
     * @brief Sends new verified block to the network
     * @param block
     */
    void SendBlock(QByteArray block, unsigned int msgType);
    /**
     * @brief Send transaction request
     * @param senderId
     * @param receiverId
     */
    void SendProveTransactionRequest(BigNumber senderId, BigNumber receiverId, QByteArray txHash);

    /**
     * @brief sends transaction request to compare transaction
     * between network and blockchain
     */
    void ApproveTX();
    /**
     * @brief Sends a compared transaaction no the network manager
     * @param Transaction compared between local blockchain and transaction
     */
    void GetTxResponse(Transaction tx);

    void finished();
};

#endif // TX_MANAGER_H
