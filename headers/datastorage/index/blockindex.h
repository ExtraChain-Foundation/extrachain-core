#ifndef BLOCKINDEX_H
#define BLOCKINDEX_H

#include "datastorage/block.h"
#include "datastorage/index/fileindex.h"
#include "datastorage/tx_pair.h"
#include "datastorage/genesis_block.h"

class BlockIndex : public FileIndex
{
public:
    BlockIndex();
    BlockIndex(const BigNumber &recordsLimit);

    /// custom folder name
    BlockIndex(const QString &folderName);
    BlockIndex(const QString &folderName, const BigNumber &recordsLimit);

public:
    /**
     * Serializes a block and make a file in fs.
     * @param block
     * @return resultCode, 0 - block is saved
     */
    int addBlock(const Block &block);

    /**
     * @brief Get last block (only Block, not Genesis block)
     * @return last block
     */
    Block getLastBlock() const;

    /**
     * @brief Get last genesis block
     * @return last genesis block
     */
    GenesisBlock getLastGenesisBlock() const;
    GenesisBlock getGenesisBlockById(const BigNumber &id) const;

    /**
     * @brief Gets block by in in file index (only Block, not Genesis block)
     * @param id
     * @return block, if is found, otherwise - empty block
     */
    Block getBlockById(const BigNumber &id) const;

    // todo: if genesis block is found -> return empty block, or skip in search logic
    Block getBlockByPosition(const BigNumber &position) const;
    Block getBlockByApprover(const BigNumber &approver) const;
    Block getBlockByHash(const QByteArray &hash) const;
    Block getBlockByData(const QByteArray &data) const;

    Block getBlockByParam(const BigNumber &id, SearchEnum::BlockParam param) const;

    Transaction getLastTxByHash(const QByteArray &hash) const;
    Transaction getLastTxBySender(const BigNumber &id) const;
    Transaction getLastTxByReceiver(const BigNumber &id) const;
    Transaction getLastTxBySenderOrReceiver(const BigNumber &id) const;
    Transaction getLastTxByApprover(const BigNumber &id) const;
    QList<Transaction> getRecentTxList(const BigNumber &last, const BigNumber &first) const;

    TxPair searchPair(const BigNumber &first, const BigNumber &second) const;

private:
    Transaction getLastTxByParam(const BigNumber &id, SearchEnum::TxParam param) const;
};

#endif // BLOCKINDEX_H
