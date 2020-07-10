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

#include "datastorage/index/memindex.h"

using std::begin, std::end, std::find_if, std::remove_if;

MemIndex::MemIndex()
{
    //
}

MemIndex::~MemIndex()
{
    //
}

int MemIndex::addBlock(const Block &block)
{
    if (blocks.contains(block))
    {
        qDebug() << "Block [" << block.toString() << "] already exists";
        return 1;
    }

    if (Config::DataStorage::MEM_INDEX_SIZE_LIMIT <= blocks.size())
    {
        qWarning() << "MemBlock is filled";
        // todo: enable FILE mode
        return 2;
    }
    blocks.append(block);
    return 0;
}

int MemIndex::removeById(const BigNumber &blockId)
{
    const int sizeWas = blocks.size();
    blocks.erase(remove_if(begin(blocks), end(blocks),
                           [&](const Block &block) { return block.getIndex() == blockId; }));
    if (sizeWas == blocks.size())
    {
        qDebug() << "There no record with id:" << blockId;
        return 1;
    }
    return 0;
}

int MemIndex::getRecords() const
{
    return blocks.size();
}

bool MemIndex::contains(const BigNumber &blockId) const
{
    Block block = this->operator[](blockId);
    return !block.isEmpty();
}

Block MemIndex::operator[](const BigNumber &blockId) const
{
    if (contains(blockId))
    {
        QList<Block>::const_iterator it = find_if(
            begin(blocks), end(blocks), [&](const Block &block) { return block.getIndex() == blockId; });
        if (it != end(blocks))
        {
            return *it;
        }
    }
    qDebug() << "There no record with id:" << blockId;
    return Block();
}

Block MemIndex::getByPosition(int pos) const
{
    return blocks.at(pos);
}

Block MemIndex::getLastBlock() const
{
    if (blocks.size() > 0)
    {
        return blocks.at(blocks.size() - 1);
    }
    return Block();
}

Block MemIndex::getBlockByParam(const BigNumber &id, SearchEnum::BlockParam param) const
{
    int index = getRecords() - 1;

    // iteration from the last to the first Block
    while (index >= 0)
    {
        Block byPosition = getByPosition(index);
        switch (param)
        {
        case SearchEnum::BlockParam::Approver: {
            if (byPosition.getApprover() == id)
                return byPosition;
            break;
        }
        case SearchEnum::BlockParam::Data: {
            if (byPosition.getData() == id)
                return byPosition;
            break;
        }
        case SearchEnum::BlockParam::Hash: {
            if (byPosition.getHash() == id)
                return byPosition;
            break;
        }
        case SearchEnum::BlockParam::Id: {
            if (byPosition.getIndex() == id)
                return byPosition;
            break;
        }
        default:
            break;
        }
        --index;
    }
    return Block();
}

Block MemIndex::getByApprover(const BigNumber &approver) const
{
    return getBlockByParam(approver, SearchEnum::BlockParam::Approver);
}

Block MemIndex::getByData(const QByteArray &data) const
{
    return getBlockByParam(data, SearchEnum::BlockParam::Data);
}

Block MemIndex::getByHash(const QByteArray &hash) const
{
    return getBlockByParam(hash, SearchEnum::BlockParam::Hash);
}

std::pair<Transaction, QByteArray> MemIndex::getLastTxByHash(const QByteArray &hash,
                                                             const QByteArray &token) const
{
    return getLastTxByParam(BigNumber(hash), SearchEnum::TxParam::Hash, token);
}

std::pair<Transaction, QByteArray> MemIndex::getLastTxBySender(const BigNumber &id,
                                                               const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserSender, token);
}

std::pair<Transaction, QByteArray> MemIndex::getLastTxByReceiver(const BigNumber &id,
                                                                 const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserReceiver, token);
}

std::pair<Transaction, QByteArray> MemIndex::getLastTxBySenderOrReceiver(const BigNumber &id,
                                                                         const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserSenderOrReceiver, token);
}

std::pair<Transaction, QByteArray>
MemIndex::getLastTxBySenderOrReceiverAndToken(const BigNumber &id, const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserSenderOrReceiverOrToken, token);
}

std::pair<Transaction, QByteArray> MemIndex::getLastTxByApprover(const BigNumber &id,
                                                                 const QByteArray &token) const
{
    return getLastTxByParam(id, SearchEnum::TxParam::UserApprover, token);
}

void MemIndex::removeAll()
{
    this->blocks.clear();
}

std::pair<Transaction, QByteArray> MemIndex::getLastTxByParam(const BigNumber &id, SearchEnum::TxParam param,
                                                              const QByteArray &token) const
{
    /*
    int records = getRecords();

    if (records == 0)
    {
        qDebug() << "There no tx's in memIndex";
        return Transaction();
    }

    int lastIndex = records - 1;

    // iterating from last to first block
    while (lastIndex >= 0)
    {
        Block byPosition = getByPosition(lastIndex);
        QList<Transaction> txs = byPosition.extractTransactions();
        for (const Transaction &tx : txs)
        {
            if (tx.getToken().toActorId() != token)
                continue;
            switch (param)
            {
            case SearchEnum::TxParam::UserSenderOrReceiverOrToken: {
                QList<QByteArray> data =
                    Serialization::deserialize(id.toActorId(), Serialization::TX_FIELD_SPLITTER);
                if (data.size() != 2)
                {
                    qDebug() << "[memindex.cpp][getLastTxByParam] Error when get Search parameter "
                                "UserSenderOrReceiverOrToken. List size !=2";
                    qInfo() << "[memindex.cpp][getLastTxByParam] Error when get Search parameter "
                               "UserSenderOrReceiverOrToken. List size !=2";
                    return Transaction();
                }

                BigNumber token = data[1];              // get value from desearizing id.
                BigNumber idSenderOrReceiver = data[0]; // get value form desearizing id.
                if ((tx.getSender() == idSenderOrReceiver && tx.getToken() == token)
                    || (tx.getReceiver() == idSenderOrReceiver && tx.getToken() == token))
                    return tx;
                break;
            }
            case SearchEnum::TxParam::UserSender: {
                if (tx.getSender() == id)
                    return tx;
                break;
            }
            case SearchEnum::TxParam::UserReceiver: {
                if (tx.getReceiver() == id)
                    return tx;
                break;
            }
            case SearchEnum::TxParam::UserSenderOrReceiver: {
                if (tx.getSender() == id || tx.getReceiver() == id)
                    return tx;
                break;
            }
            case SearchEnum::TxParam::UserApprover: {
                if (tx.getApprover() == id)
                    return tx;
                break;
            }
            case SearchEnum::TxParam::Hash: {
                if (tx.getHash() == id.toActorId())
                    return tx;
                break;
            }
            default: {
            }
            }
        }
        --lastIndex;
    }
    */

    return { Transaction(), "-1" };
}
