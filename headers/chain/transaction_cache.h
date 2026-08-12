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

#include <QObject>

#include "chain/actor_id.h"
#include "chain/transaction.h"

class ExtraChainNode;
class Transaction;

class TransactionCache : public QObject {
    Q_OBJECT

public:
    explicit TransactionCache(ExtraChainNode *node, QObject *parent);
    void make_files();

signals:
    void add(const Transaction &transaction);
    void request(ActorId actor_id, TokenId token, bool reward_hidden, std::uint64_t from_time);
    void response(ActorId actor_id, TokenId token, int offset, std::vector<TransactionInfo> txs);
    void make_cache();

private slots:
    void adding(const Transaction &transaction);
    void prepare(ActorId actor_id, TokenId token, bool reward_hidden, std::uint64_t from_time);
    void cache();

private:
    ExtraChainNode *node;

    friend class Dag;
};
