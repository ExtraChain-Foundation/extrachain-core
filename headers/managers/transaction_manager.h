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

#include "datastorage/transaction.h"

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

    ExtraChainNode *node;
    // received transactions that we need to compare between network and blockchain

public:
    // todo: add ref to blockchain
    TransactionManager(ExtraChainNode *node);

private:
    void addTransaction(const Transaction &tx);
    void proveTransactions();
    void addProvedTransaction(const Transaction &tx);
    void makeBlock();
    void removeTransaction(int i);

    friend class NetworkManager;

public:
    BigNumberFloat        checkPendingTxsList(const ActorId &sender);
    std::set<Transaction> getReceivedTxList() const;
    std::set<Transaction> getPendingTxs() const;
    /**
     * Run make_block and prove_block timers
     */
    void runMakeAndProveBlockTimers();

public slots:
    void makeBlockAndProveTransactionsInThread();
    void process() {};

signals:
    void finished();
    void addToCache(std::string actor, Transaction tx);
};

#endif // TRANSACTION_MANAGER_H
