#ifndef MEMINDEX_H
#define MEMINDEX_H

#include <QMap>
#include <QDebug>
#include <algorithm>
#include "datastorage/block.h"
#include "utils/utils.h"
#include "datastorage/tx_pair.h"

using std::begin;
using std::end;
using std::find_if;
using std::remove_if;

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
    TxPair searchPair(const BigNumber &senderId, const BigNumber &receiverId) const;

    void removeAll();

private:
    std::pair<Transaction, QByteArray> getLastTxByParam(const BigNumber &id, SearchEnum::TxParam param,
                                                        const QByteArray &token) const;
};

#endif // MEMINDEX_H
