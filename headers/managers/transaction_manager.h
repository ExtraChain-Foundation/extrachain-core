/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once

#include <QByteArray>
#include <QObject>
#include <QThread>
#include <QTimer>

#include "blockchain/transaction.h"

class ExtraChainNode;

/**
 * @brief Process all incoming transactions
 * Approves and packs them into a new block
 */
class EXTRACHAIN_EXPORT TransactionManager : public QObject {
    Q_OBJECT

private:
    // to create block's from pending txs
    QTimer *timer_make_block;

    // received transactions that will be packed into block
    std::set<Transaction> unproved_transactions_;
    std::set<Transaction> proved_transactions_;

    ExtraChainNode *node;
    // received transactions that we need to compare between network and blockchain

public:
    // todo: add ref to blockchain
    TransactionManager(ExtraChainNode *node);

private:
    void prove_transactions();
    void add_proved_transaction(const Transaction &tx);
    void make_block();

    friend class NetworkManager;

public:
    std::set<Transaction> unproved_transactions() const;
    std::set<Transaction> proved_transactions() const;

public slots:
    void timer_block_tick();
    void network_add_transaction(const Transaction &tx);
    void process();

signals:
    void finished();
    void addToCache(std::string actor, Transaction tx);
    void network_add_transaction_signal(const Transaction &tx);
};
