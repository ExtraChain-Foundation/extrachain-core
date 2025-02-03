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

#include "blockchain/actor.h"
#include "blockchain/block.h"
#include "blockchain/genesis_block.h"
#include "blockchain/block_index.h"
#include "blockchain/transaction.h"
#include "network/network_manager.h"
#include "utils/bignumber.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>
#include <cassert>

class TransactionManager;
class Responder;

/*
 * Main database class
 *
 * Is responding for:
 * - saving blocks
 * - validating blocks
 * - merging blocks
 *
 */

class ExtraChainNode;

enum class BlockchainStatus {
    // Process,
    Started,
    Ready,
    Sync,
    Maybe,
    Timered,
};

enum class BlockchainSyncStatus {
    None,
    LastInfo,
    Blocks
};

struct BlockchainLastInfo {
    BigNumber   last_block_id;
    std::string last_hash;
};
BOOST_DESCRIBE_STRUCT(BlockchainLastInfo, (), (last_block_id, last_hash))

class EXTRACHAIN_EXPORT Blockchain : public QObject {
    //    static_assert(is_same<T, Block>::value || is_same<T, GenesisBlock>::value,
    //                  "Your type is not supported."
    //                  "Supportable types: BigNumber, Transaction, Block, TxPair, Actor");
    Q_OBJECT

    friend class ExtraChainNode;

private:
    ExtraChainNode *node;

    // storage //
    BlockIndex blockIndex; // blocks (if fileMode is true)
    // service //

    BlockchainStatus                                    status_        = BlockchainStatus::Started;
    BlockchainSyncStatus                                sync_status_   = BlockchainSyncStatus::None;
    BlockchainSyncStatus                                check_status_  = BlockchainSyncStatus::None;
    int                                                 requests_count = 0;
    std::unordered_map<std::string, BlockchainLastInfo> last_info_;

    QTimer *timer_sync;

public:
    explicit Blockchain(ExtraChainNode *node);
    std::expected<BlockVariant, BlockError> getBlockByHash(const std::string &hash);
    ~Blockchain();

    std::expected<BlockVariant, BlockError> getBlockByIndex(const BigNumber &index,
                                                            const bool       makeRequestBlock = false);
    std::pair<Transaction, BigNumber>       getTxByHash(const std::string &hash, const TokenId &token = TokenId());

    void sync(const BigNumber &from = BigNumber(), std::optional<Responder> responder = std::nullopt);
    void lastSavedRequest();

    BlockchainStatus status();

    void start_sync();
    void start_check();
    // to slot
    void network_status_sync_request(const Responder &responder);
    // to slot
    void network_status_sync_response(const BlockchainLastInfo &last_info, const Responder &responder);
    void send_request_blocks();

    void remove_last_block() {
        auto block = this->getLastRealBlock();

        if (block.has_value() && block->getIndex() != BigNumber(0)) {
            blockIndex.removeById(block->getIndex());
            eLog("[Blockchain] Remove last block: {}", block->getIndex());
        }
    }

private:
    std::expected<BlockVariant, BlockError> getBlockByData(const std::string &data);

    std::pair<Transaction, BigNumber> getTxBySender(const ActorId &id, const TokenId &token = TokenId());
    std::pair<Transaction, BigNumber> getTxByReceiver(const ActorId &id, const TokenId &token = TokenId());
    std::pair<Transaction, BigNumber> getTxBySenderOrReceiver(const ActorId &id, const TokenId &token = TokenId());
    std::pair<Transaction, BigNumber> getTxBySenderOrReceiverAndToken(const ActorId &id,
                                                                      const ActorId &token = TokenId());
    std::pair<Transaction, BigNumber> getTxByUser(const ActorId &id, const TokenId &token = TokenId());

    // genesis blocks //
    QByteArray findRecordsInBlock(const BlockVariant &block);
    bool       signCheckAdd(BlockVariant &block);

public:
    static BigNumber lastGenesisIdFor(const BigNumber &id);
    static bool      isGenesisId(const BigNumber &id);

    std::expected<BlockVariant, BlockError> createGenesisBlock(const Actor<KeyPrivate> &actor);

    std::expected<BlockVariant, BlockError> createFirstBlock(
        const Actor<KeyPrivate> &actor /*, std::map<std::pair<ActorId, TokenId>, GenesisDataRow> dataRows = {}*/);

    std::set<Transaction> getTxsBySenderOrReceiverInRow(const BigNumber &id,
                                                        BigNumber        from  = BigNumber(-1),
                                                        int              count = 10,
                                                        ActorId          token = ActorId());

    bool sendBlock(const BlockVariant &block) const;
    void sendBlockByNumber(const BigNumber &index) const;
    void sendLastGenesisBlock() const;

private:
    // merging //
    std::expected<BlockVariant, BlockError> mergeBlockWithLocal(const BlockVariant &received);

    /**
     * @brief validates block digital signature
     * @param block
     * @return true if block is valid
     */
    bool validateBlock(const BlockVariant &block);
    /**
     * @brief validates block using validateBlock method
     * @param block
     * @return block - if it is valid, empty block - if block is corrupted.
     */
    BlockVariant validateAndReturnBlock(const BlockVariant &block) const;

public:
    void updateFirstId(const BlockVariant &block);

    // - BLOCKS - //

    /**
     * @return last blockchain block
     */
    std::expected<BlockVariant, BlockError> getLastBlock() const;

    /**
     * @return Amount of blockchain blocks
     */
    BigNumber getBlocksStored() const;

    /**
     * @return last real blockchain block
     */
    std::expected<BlockVariant, BlockError> getLastRealBlock() const;

    /**
     * Gets the block from blockchain by *value* of a certain *type*
     * @param value
     * @param type of param
     * @return last blockchain block
     */
    std::expected<BlockVariant, BlockError> getBlock(SearchEnum::BlockParam type, const std::string &value);

    /**
     * Gets the transaction from blockchain by *value* of a certain *type*
     * @param value
     * @param type of param
     * @return transaction
     */
    std::pair<Transaction, BigNumber> getTransaction(SearchEnum::TxParam type,
                                                     const std::string  &value,
                                                     const TokenId      &token = TokenId());

private:
    /**
     * Add block to blockchain
     * Convert block to MemBlock or FileBlock according to a fileMode.
     * @return 0 is success, or error code
     */
    std::expected<BlockVariant, BlockError> addBlock(const BlockVariant &block);
    std::expected<BlockVariant, BlockError> replaceBlock(const BlockVariant &block);

public:
    /**
     * Removes block and all blocks after them
     * @return 0 is success, or error code
     */
    int removeBlock(const BlockVariant &block);

    /**
     *
     */
    void removeDummyBlocks();

    /**
     * @brief Check if two blocks can be merged
     * (has identical id and at least one common transaction)
     * @param blockA
     * @param blockB
     * @return true, if blocks can be merged
     */
    bool canMergeBlocks(const BlockVariant &receivedBlock, const BlockVariant &existedBlock);
    /**
     * @brief Merge two blocks to one and sign it using approver
     * @param blockA
     * @param blockB
     * @return merged block
     */
    std::expected<BlockVariant, BlockError> mergeBlocks(const Block &blockA, const Block &blockB);
    std::expected<BlockVariant, BlockError> mergeGenesisBlocks(const GenesisBlock &blockA,
                                                               const GenesisBlock &blockB);

    /**
     * @brief Sign Block with current approver
     * @param block with signature
     */
    void signBlock(BlockVariant &block) const;

    // - ACTORS - //
    /**
     * @brief remove all blocks
     */
    void removeAll();

    /**
     * @brief Return's reference to blockIndex
     * @return ref to blockIndex field
     */
    BlockIndex &getBlockIndex();

    /**
     * @brief Gets last block data field
     * @return data
     */
    QString getLastBlockData() const;
    /**
     * @brief getRecords
     * @return records
     */
    BigNumber getRecords() const;
    /**
     * @brief getCountRealBlockRecords
     * @return count real blocks
     */
    BigNumber getCountRealBlockRecords() const;

    /**
     * @brief getCountTransactionsInBlocks
     * @return count transactions in blocks
     */
    int getCountTransactionsInBlocks() const;

    BigNumberFloat getUserBalance(ActorId         userId,
                                  TokenId         tokenId = TokenId(),
                                  TransactionType txType  = TransactionType::Regular) const;

    /**
     * @brief Show blockchain
     */
    void showBlockchain() const;

    void getSmContractMembers(const BlockVariant &block) const;

signals:
    void finished();
    void newNotify(Notification ntf);
    void updateLastTransactionList();
    void blockAdded(const BlockVariant block);
    void updateSelf(BigNumber blockId);
    void addBlockFromNetwork(const BlockVariant &block,
                             const Responder    &responder,
                             const NetworkPackageStorage,
                             bool resend);
    void syncResponseFromNetwork(const BigNumber fromBlock, const Responder &responder);
    void syncResponseVectorFromNetwork(std::vector<BlockVariant> blocks,
                                       const Responder          &responder,
                                       const NetworkPackageStorage);
    void statusChanged(BlockchainStatus status);

    /**
     * @brief possibleMiningChange
     * @param possibleMinig
     */
    void possibleMiningChange(const bool &possibleMinig);

public:
    BigNumber getBlockCount();

    /**
     * @brief finds needed transaction by sender or receiver
     */
    TransactionProveError proveTransaction(const Transaction &tx, const std::set<Transaction> transactions);

public slots:
    void addBlockNetwork(const BlockVariant &block,
                         const Responder    &responder,
                         const NetworkPackageStorage,
                         bool resend);
    void syncResponse(const BigNumber fromBlock, const Responder &responder);
    void syncResponseVector(std::vector<BlockVariant>    blocks,
                            const Responder             &responder,
                            const NetworkPackageStorage &package_storage);
    void timer_sync_tick();
    void process();
};
