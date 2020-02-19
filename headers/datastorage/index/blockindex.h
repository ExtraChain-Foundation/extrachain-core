#ifndef BLOCKINDEX_H
#define BLOCKINDEX_H

#include "datastorage/block.h"
#include "datastorage/tx_pair.h"
#include "datastorage/genesis_block.h"
#include "utils/db_connector.h"

class BlockIndex
{
public:
    BlockIndex();
    BlockIndex(const BigNumber &recordsLimit);

    /// custom folder name
    BlockIndex(const QString &folderName);
    BlockIndex(const QString &folderName, const BigNumber &recordsLimit);

    QString folderName;          // set in subclasses
    int sectionSize;             // todo: 0 = use only one folder
    BigNumber recordsLimit = -1; // -1 = no limit

    // current state //
    BigNumber records = 0;
    BigNumber firstSavedId = -1;
    BigNumber lastSavedId = -1;

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

    QByteArray getBlockDataById(const BigNumber &id) const;

    // todo: if genesis block is found -> return empty block, or skip in search logic
    Block getBlockByPosition(const BigNumber &position) const;
    Block getBlockByApprover(const BigNumber &approver) const;
    Block getBlockByHash(const QByteArray &hash) const;
    Block getBlockByData(const QByteArray &data) const;

    Block getBlockByParam(const BigNumber &id, SearchEnum::BlockParam param) const;

    Transaction getLastTxByHash(const QByteArray &hash, const QByteArray &token) const;
    Transaction getLastTxBySender(const BigNumber &id, const QByteArray &token) const;
    Transaction getLastTxByReceiver(const BigNumber &id, const QByteArray &token) const;
    Transaction getLastTxBySenderOrReceiver(const BigNumber &id, const QByteArray &token) const;
    Transaction getLastTxBySenderOrReceiverAndToken(const BigNumber &id, const QByteArray &token) const;
    Transaction getLastTxByApprover(const BigNumber &id, const QByteArray &token) const;
    QList<Transaction> getRecentTxList(const BigNumber &last, const BigNumber &first) const;

    QList<Transaction> getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from = -1, int count = 10,
                                                     BigNumber token = 0) const;

    TxPair searchPair(const BigNumber &first, const BigNumber &second) const;

    void removeAll();
    BigNumber getLastSavedId() const;
    BigNumber getFirstSavedId() const;
    BigNumber getRecords() const;
    int removeById(const BigNumber &id);
    QString buildFilePath(const BigNumber &id) const;

private:
    Transaction getLastTxByParam(const BigNumber &id, SearchEnum::TxParam param,
                                 const QByteArray &token) const;
    QList<Transaction> getTxsByParamInRow(const BigNumber &id, SearchEnum::TxParam param, BigNumber from = -1,
                                          int count = 10, BigNumber token = 0) const;

    int add(const BigNumber &id, const QByteArray &_data);
    bool hasRecordLimit() const;
    bool recordLimitIsReached() const;
    QString getFolderPath() const;
    QString getFolderName() const;
    BigNumber calcSection(BigNumber id) const;
    QByteArray getById(const BigNumber &id) const;
    BigNumber loadFirstId();
    BigNumber loadFileFromSection(std::function<QString(const QStringList &folders)> getFolder,
                                  std::function<QString(const QStringList &files)> getFile);

    BigNumber loadLastId();
};

#endif // BLOCKINDEX_H
