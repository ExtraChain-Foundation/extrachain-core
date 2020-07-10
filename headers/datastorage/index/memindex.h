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

#ifndef MEMINDEX_H
#define MEMINDEX_H

#include <QMap>
#include <QDebug>
#include <algorithm>
#include "datastorage/block.h"
#include "utils/utils.h"

class MemIndex
{
private:
    QList<Block> blocks;

public:
    MemIndex();
    ~MemIndex();

public:
    int addBlock(const Block &block);
    int removeById(const BigNumber &blockId);
    int getRecords() const;

public:
    bool contains(const BigNumber &blockId) const;
    Block operator[](const BigNumber &blockId) const;
    Block getByPosition(int pos) const;
    Block getLastBlock() const;
    Block getBlockByParam(const BigNumber &id, SearchEnum::BlockParam) const;
    Block getByApprover(const BigNumber &approver) const;
    Block getByData(const QByteArray &data) const;
    Block getByHash(const QByteArray &hash) const;

    std::pair<Transaction, QByteArray> getLastTxByHash(const QByteArray &hash, const QByteArray &token) const;
    std::pair<Transaction, QByteArray> getLastTxBySender(const BigNumber &id, const QByteArray &token) const;
    std::pair<Transaction, QByteArray> getLastTxByReceiver(const BigNumber &id,
                                                           const QByteArray &token) const;
    std::pair<Transaction, QByteArray> getLastTxBySenderOrReceiver(const BigNumber &id,
                                                                   const QByteArray &token) const;
    std::pair<Transaction, QByteArray> getLastTxBySenderOrReceiverAndToken(const BigNumber &id,
                                                                           const QByteArray &token) const;
    std::pair<Transaction, QByteArray> getLastTxByApprover(const BigNumber &id,
                                                           const QByteArray &token) const;
    void removeAll();

private:
    std::pair<Transaction, QByteArray> getLastTxByParam(const BigNumber &id, SearchEnum::TxParam param,
                                                        const QByteArray &token) const;
};

#endif // MEMINDEX_H
