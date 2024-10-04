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

#ifndef TRANSACTION_MANAGER_H
#define TRANSACTION_MANAGER_H

#include <QByteArray>
#include <QDebug>
#include <QList>
#include <QObject>
#include <QThread>
#include <QTimer>

#include "datastorage/block.h"
#include "datastorage/blockchain.h"
#include "datastorage/index/blockindex.h"
#include "datastorage/transaction.h"
#include "data_mining_manager.h"

class ExtraChainNode;

/**
 * @brief Process all incoming transactions
 * Approves and packs them into a new block
 */
class EXTRACHAIN_EXPORT TransactionManager : public QObject {
    Q_OBJECT

private:
    // to create block's from pending txs
    QTimer blockCreationTimer;
    QTimer proveTimer;

    // received transactions that will be packed into block
    std::set<Transaction> m_pendingTxList;
    std::set<Transaction> m_receivedTxList;

    ExtraChainNode& node;
    // received transactions that we need to compare between network and blockchain

public:
    // todo: add ref to blockchain
    TransactionManager(ExtraChainNode &node);

private:
    void addProvedTransaction(const Transaction &tx);
    void removeTransaction(int i);

public:
    BigNumberFloat checkPendingTxsList(const ActorId &sender);
    std::set<Transaction> getReceivedTxList() const;
    std::set<Transaction> getPendingTxs() const;
    /**
     * Run make_block and prove_block timers
     */
    void runMakeAndProveBlockTimers();

public slots:
    /**
     * Serialize all transactions to a serialized data.
     * Creates a memblock, and setup data field with serialized data.
     * Emits SendBlock signal.
     */
    void makeBlock();
    void makeBlockAndProveTransactionsInThread();

    /**
     * If Transaction is valid, adds it to the txList.
     * @param tx - transaction
     * @return 0 is transaction is successfully added
     */

    void proveTransactions();
    void addTransaction(const Transaction &tx);
    // Unapproved tx's //

    /**
     * @brief isUnaproved
     * @param txHash
     * @return true if there is txHash in unApprovedTxHashes, false otherwise
     */
    // bool isUnapproved(const std::string &txHash);

    /**
     * @brief Removes hash from unApprovedTxHashes list
     * @param txHash to remove
     */
    // void removeUnapprovedHash(const std::string &txHash);

    /**
     * @brief addUnapprovedHash
     * @param txHash
     */
    // void addUnapprovedHash(const std::string &txHash);

    /**
     * @brief Adds transaction to pending list
     * @param tx - already verified transaction
     */
    // void addVerifiedTx(Transaction tx);

signals:
    /**
     * @brief Signal to blockchain. We need to enshure, that this is really new tx.
     * (By checking tx existanse in previous blocks)
     * @param tx - transaction to check
     */
    // void VerifyTx(Transaction tx);

    /**
     * @brief Sends new verified block to the network
     * @param block
     */
    // void SendBlock(QByteArray block, unsigned int msgType);
    /**
     * @brief Send transaction request
     * @param senderId
     * @param receiverId
     */
    // void SendProveTransactionRequest(BigNumber senderId, BigNumber receiverId, QByteArray txHash);

    /**
     * @brief sends transaction request to compare transaction
     * between network and blockchain
     */
    // void ApproveTX();
    /**
     * @brief Sends a compared transaaction no the network manager
     * @param Transaction compared between local blockchain and transaction
     */
    // void GetTxResponse(Transaction tx);

    void finished();

    void addToCache(std::string actor, Transaction tx);
};

#endif // TRANSACTION_MANAGER_H
