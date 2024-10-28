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

#ifndef BLOCKINDEX_H
#define BLOCKINDEX_H

#include <list>

#include "datastorage/block.h"
#include "datastorage/genesis_block.h"
#include "datastorage/block_variant.h"
#include "utils/db_connector.h"

class EXTRACHAIN_EXPORT BlockIndex {
public:
    BlockIndex();
    explicit BlockIndex(const BigNumber &recordsLimit);

    /// custom folder name
    explicit BlockIndex(const QString &folderName);
    explicit BlockIndex(const QString &folderName, const BigNumber &recordsLimit);

    QString   folderName;        // set in subclasses
    int       sectionSize;       // todo: 0 = use only one folder
    BigNumber recordsLimit = -1; // -1 = no limit

    // current state //
    BigNumber records           = 0;
    BigNumber firstSavedId      = -1;
    BigNumber lastSavedId       = -1;
    BigNumber realBlockRecords  = 0;
    int       countTransactions = 0;

    bool m_blockCompress = false;

public:
    void setBlockCompress(bool newBlockCompress);

    /**
     * Serializes a block and make a file in fs.
     * @param block
     * @return resultCode, 0 - block is saved
     */
    std::expected<BlockVariant, BlockError> addBlock(const BlockVariant &block);

    /**
     * @brief Get last block (only Block, not Genesis block)
     * @return last block
     */
    std::expected<BlockVariant, BlockError> getLastBlock() const;

    /**
     * @brief Get last real (not dummy) block
     * @return last real block
     */
    std::expected<BlockVariant, BlockError> getLastRealBlock() const;
    /**
     * @brief Get last genesis block
     * @return last genesis block
     */
    std::expected<BlockVariant, BlockError> getLastGenesisBlock(const BigNumber &from = -1) const;
    std::expected<BlockVariant, BlockError> getGenesisBlockById(const BigNumber &id) const;

    /**
     * @brief Gets block by in in file index (only Block, not Genesis block)
     * @param id
     * @return block, if is found, otherwise - empty block
     */
    std::expected<BlockVariant, BlockError> getBlockById(const BigNumber &id) const;

    // todo: if genesis block is found -> return empty block, or skip in search logic
    std::expected<BlockVariant, BlockError> getBlockByPosition(const BigNumber &position) const;
    std::expected<BlockVariant, BlockError> getBlockByHash(const std::string &hash) const;
    std::expected<BlockVariant, BlockError> getBlockByData(const std::string &data) const;

    std::expected<BlockVariant, BlockError>
    getBlockByParam(const BigNumber &id, SearchEnum::BlockParam param) const;

    std::pair<Transaction, BigNumber> getLastTxByHash(const std::string &hash, const TokenId &token) const;
    std::pair<Transaction, BigNumber> getLastTxByData(const std::string &data, const TokenId &token) const;
    std::pair<Transaction, BigNumber> getLastTxBySender(const ActorId &id, const TokenId &token) const;
    std::pair<Transaction, BigNumber> getLastTxByReceiver(const ActorId &id, const TokenId &token) const;
    std::pair<Transaction, BigNumber>
    getLastTxBySenderOrReceiver(const ActorId &id, const TokenId &token) const;
    std::pair<Transaction, BigNumber>
    getLastTxBySenderOrReceiverAndToken(const ActorId &id, const TokenId &token) const;
    std::pair<Transaction, BigNumber> getLastTxByApprover(const ActorId &id, const TokenId &token) const;
    // std::vector<Transaction> getRecentTxList(const BigNumber &last, const BigNumber &first) const;

    std::set<Transaction> getTxsBySenderOrReceiverInRow(
        const BigNumber &id,
        BigNumber        from  = -1,
        int              count = 10,
        const ActorId   &token = ActorId()) const;

    void      removeAll();
    BigNumber getLastSavedId() const;
    BigNumber getFirstSavedId() const;
    BigNumber getRecords() const;
    BigNumber getCountRealBlocks() const;
    int       getCountTransactionsInBlocks() const;
    int       removeById(const BigNumber &id);
    int       removeById(const BlockVariant &block);
    void      removeDummyBlocks();
    QString   buildFilePath(const BigNumber &id) const;

    void calculationCountBlock();

private:
    std::pair<Transaction, BigNumber> getLastTxByParam(const std::string &data, SearchEnum::TxParam param, const ActorId &token) const;
    std::set<Transaction> getTxsByParamInRow(
        const BigNumber    &id,
        SearchEnum::TxParam param,
        BigNumber           from  = -1,
        int                 count = 10,
        ActorId             token = ActorId()) const;

    std::expected<BlockVariant, BlockError> add(const BigNumber &id, const BlockVariant &newBlock);
    bool                                    hasRecordLimit() const;
    bool                                    recordLimitIsReached() const;
    QString                                 getFolderPath() const;
    QString                                 getFolderName() const;
    BigNumber                               calcSection(BigNumber id) const;
    std::expected<BlockVariant, BlockError> getByIdUnsafe(const BigNumber &id) const;
    std::expected<BlockVariant, BlockError> getById(const BigNumber &id) const;
    BigNumber                               loadFirstId();
    BigNumber                               loadFileFromSection(
                                      std::function<QString(const QStringList &folders)> getFolder,
                                      std::function<QString(const QStringList &files)>   getFile);

    BigNumber loadLastId();
};

#endif // BLOCKINDEX_H
