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

#include <list>

#include "blockchain/block.h"
#include "blockchain/genesis_block.h"
#include "blockchain/block_variant.h"
#include "utils/db_connector.h"

struct BlockRange {
    std::string first;
    std::string last;
};
BOOST_DESCRIBE_STRUCT(BlockRange, (), (first, last))

class EXTRACHAIN_EXPORT BlockIndex {
public:
    BlockIndex();
    // explicit BlockIndex(const BigNumber &recordsLimit);

    int       sectionSize;                  // todo: 0 = use only one folder
    BigNumber recordsLimit = BigNumber(-1); // -1 = no limit

    // current state //
    // BigNumber records           = BigNumber(0);
    BigNumber first_saved_id = BigNumber(-1);
    BigNumber last_saved_id  = BigNumber(-1);
    // int       countTransactions = 0;

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
     * @brief Get last genesis block
     * @return last genesis block
     */
    std::expected<BlockVariant, BlockError> getLastGenesisBlock(const BigNumber &from = BigNumber(-1)) const;
    std::expected<BlockVariant, BlockError> getGenesisBlockById(const BigNumber &id) const;

    /**
     * @brief Gets block by in in file index (only Block, not Genesis block)
     * @param id
     * @return block, if is found, otherwise - empty block
     */
    std::expected<BlockVariant, BlockError> read_block_by_id(const BigNumber &id) const;

    // todo: if genesis block is found -> return empty block, or skip in search logic
    std::expected<BlockVariant, BlockError> search_block_by_hash(const std::string &hash) const;
    std::expected<BlockVariant, BlockError> getBlockByData(const std::string &data) const;

    std::expected<BlockVariant, BlockError> getBlockByParam(const std::string     &id,
                                                            SearchEnum::BlockParam param) const;
    std::pair<Transaction, BigNumber>       search_duplicate(const std::string &hash) const;
    std::pair<Transaction, BigNumber>       getLastTxByHash(const std::string &hash, const TokenId &token) const;
    std::pair<Transaction, BigNumber>       getLastTxByData(const std::string &data, const TokenId &token) const;
    std::pair<Transaction, BigNumber>       getLastTxBySender(const ActorId &id, const TokenId &token) const;
    std::pair<Transaction, BigNumber>       getLastTxByReceiver(const ActorId &id, const TokenId &token) const;
    std::pair<Transaction, BigNumber> getLastTxBySenderOrReceiver(const ActorId &id, const TokenId &token) const;
    std::pair<Transaction, BigNumber> getLastTxBySenderOrReceiverAndToken(const ActorId &id,
                                                                          const TokenId &token) const;
    // std::vector<Transaction> getRecentTxList(const BigNumber &last, const BigNumber &first) const;

    std::unordered_map<ActorId, std::vector<TransactionInfo>> getTxsBySenderOrReceiverInRow(
        const std::vector<ActorId> &actor_ids,
        BigNumber                   from  = BigNumber(-1),
        int                         count = 10,
        const ActorId              &token = ActorId()) const;

    void        removeAll();
    BigNumber   getLastSavedId() const;
    BigNumber   getFirstSavedId() const;
    int         getCountTransactionsInBlocks() const;
    int         removeById(const BigNumber &id);
    int         removeById(const BlockVariant &block);
    std::string buildFilePath(const BigNumber &id) const;

    void update_last_id(const BigNumber &id);

private:
    std::pair<Transaction, BigNumber> getLastTxByParam(const std::string  &data,
                                                       SearchEnum::TxParam param,
                                                       const TokenId      &tokenId) const;

    std::unordered_map<ActorId, std::vector<TransactionInfo>> getTxsByParamInRow(
        const std::vector<ActorId> &actor_ids,
        SearchEnum::TxParam         param,
        BigNumber                   from  = BigNumber(-1),
        int                         count = 10,
        ActorId                     token = ActorId()) const;

    std::expected<BlockVariant, BlockError> add(const BigNumber &id, const BlockVariant &newBlock);
    bool                                    hasRecordLimit() const;
    // bool                                    recordLimitIsReached() const;
    std::string                             getFolderPath() const;
    BigNumber                               calcSection(BigNumber id) const;
    std::expected<BlockVariant, BlockError> getByIdUnsafe(const BigNumber &id) const;
    std::expected<BlockVariant, BlockError> getById(const BigNumber &id) const;
};
