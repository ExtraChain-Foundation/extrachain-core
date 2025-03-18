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
#include "blockchain/transaction_cache.h"

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

enum class BlockchainMode {
    Full,
    Light
};

struct BlockchainLastInfo {
    BigNumber     last_block_id;
    std::string   last_hash;
    std::uint64_t zero_date;
};
BOOST_DESCRIBE_STRUCT(BlockchainLastInfo, (), (last_block_id, last_hash, zero_date))

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

    TransactionCache transaction_cache_;

    BlockchainStatus                                    status_       = BlockchainStatus::Started;
    BlockchainSyncStatus                                sync_status_  = BlockchainSyncStatus::None;
    BlockchainSyncStatus                                check_status_ = BlockchainSyncStatus::None;
    BigNumber                                           sync_last_index;
    int                                                 requests_count = 0;
    std::unordered_map<std::string, BlockchainLastInfo> last_info_;

    BlockchainMode mode = BlockchainMode::Full;

    QTimer *timer_sync;

public:
    explicit Blockchain(ExtraChainNode *node);
    std::expected<BlockVariant, BlockError> search_block_by_hash(const std::string &hash);
    ~Blockchain();

    std::expected<BlockVariant, BlockError> read_block_by_id(const BigNumber &id,
                                                             const bool       makeRequestBlock = false);
    std::pair<Transaction, BigNumber> search_tx_by_hash(const std::string &hash, const TokenId &token = TokenId());

    void sync(const BigNumber &from = BigNumber(), std::optional<Responder> responder = std::nullopt);

    BlockchainStatus status();

    void start_sync();
    void start_check();
    // to slot
    void network_status_sync_request(const Responder &responder);
    // to slot
    void network_status_sync_response(const BlockchainLastInfo &last_info, const Responder &responder);
    void send_request_blocks();

    void remove_last_block() {
        auto block = this->read_last_block();

        if (block.has_value() && block->id() != BigNumber(0)) {
            blockIndex.removeById(block->id());
            eLog("[Blockchain] Remove last block: {}", block->id());
        }
    }

private:
    void set_sync_status(BlockchainSyncStatus status) {
        sync_status_ = status;
        syncStatusChanged(status);
    }

    std::expected<BlockVariant, BlockError> getBlockByData(const std::string &data);

    std::pair<Transaction, BigNumber> getTxBySender(const ActorId &id, const TokenId &token = TokenId());
    std::pair<Transaction, BigNumber> getTxByReceiver(const ActorId &id, const TokenId &token = TokenId());
    std::pair<Transaction, BigNumber> getTxBySenderOrReceiver(const ActorId &id, const TokenId &token = TokenId());
    std::pair<Transaction, BigNumber> getTxBySenderOrReceiverAndToken(const ActorId &id,
                                                                      const ActorId &token = TokenId());
    std::pair<Transaction, BigNumber> getTxByUser(const ActorId &id, const TokenId &token = TokenId());

    // genesis blocks //

public:
    static BigNumber calculate_genesis_id_for_block(const BigNumber &id);
    static bool      is_genesis_id(const BigNumber &id);

    std::expected<BlockVariant, BlockError> create_genesis_block(const Actor<KeyPrivate> &actor);
    std::expected<BlockVariant, BlockError> create_mega_genesis_block(const Actor<KeyPrivate> &actor);

    std::pair<int, int> active_users();

    std::expected<BlockVariant, BlockError> create_zero_genesis_block(
        const Actor<KeyPrivate> &actor /*, std::map<std::pair<ActorId, TokenId>, GenesisDataRow> dataRows = {}*/);

    std::unordered_map<ActorId, std::vector<TransactionInfo>> getTxsBySenderOrReceiverInRow(
        const std::vector<ActorId> &id,
        BigNumber                   from  = BigNumber(-1),
        int                         count = 10,
        ActorId                     token = ActorId());

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
    void update_network_id(const BlockVariant &block);

    // - BLOCKS - //

    /**
     * @return last blockchain block
     */
    std::expected<BlockVariant, BlockError> read_last_block() const;

    /**
     * @return Amount of blockchain blocks
     */
    BigNumber getBlocksStored() const;

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
     * @brief Return's reference to blockIndex
     * @return ref to blockIndex field
     */
    BlockIndex &getBlockIndex();

    TransactionCache &transaction_cache() {
        return transaction_cache_;
    }

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

    BigNumberFloat                              calculate_actor_balance(const ActorId &actor_id,
                                                                        const TokenId &token_id,
                                                                        bool           ignore_genesis = false) const;
    std::unordered_map<ActorId, BigNumberFloat> calculate_actors_balance(const std::vector<ActorId> &actor_ids,
                                                                         const TokenId              &token_id,
                                                                         bool ignore_genesis = false) const;

    /**
     * @brief Show blockchain
     */
    void showBlockchain() const;

    void getSmContractMembers(const BlockVariant &block) const;

signals:
    void need_check();
    void finished();
    void newNotify(Notification ntf);
    void updateLastTransactionList();
    void blockAdded(const BlockVariant block);
    void updateSelf(BigNumber blockId);

    void selfTxAdded(const BigNumber &block_id, uint64_t block_date, const Transaction &transaction);
    void selfTxRepeatableAdded(const BigNumber &block_id, uint64_t block_date, const Transaction &transaction);

    void addBlockFromNetwork(const BlockVariant &block,
                             const Responder    &responder,
                             const NetworkPackageStorage,
                             bool resend);
    void syncResponseFromNetwork(const BigNumber fromBlock, const Responder &responder);
    void syncResponseVectorFromNetwork(const std::string &blocks,
                                       const Responder   &responder,
                                       const NetworkPackageStorage);

    void zeroBlock();

    void removeAll(bool is_mega = false, bool is_exit = false);

    /**
     * @brief possibleMiningChange
     * @param possibleMinig
     */
    void possibleMiningChange(const bool &possibleMinig);

    void network_status_sync_request_signal(const Responder &responder);
    void network_status_sync_response_signal(const BlockchainLastInfo &last_info, const Responder &responder);

    void syncStart(BigNumber, BigNumber);
    void syncEnd();
    void syncProgress(BigNumber);
    void statusChanged(BlockchainStatus status);
    void syncStatusChanged(BlockchainSyncStatus);
    void resultTransactions(const std::unordered_map<ActorId, std::vector<Transaction>> &);
    void testSignal();

public:
    BigNumber getBlockCount();

    /**
     * @brief finds needed transaction by sender or receiver
     */
    TransactionProveError prove_transaction(const Transaction &tx, const std::set<Transaction> transactions);

    BlockchainMode getMode() const;
    void           setMode(BlockchainMode newMode);

public slots:
    std::expected<BlockVariant, BlockError> addBlockNetwork(const BlockVariant &block,
                                                            const Responder    &responder,
                                                            const NetworkPackageStorage,
                                                            bool resend);

    void syncResponse(const BigNumber &fromBlock, const Responder &responder);
    void syncResponseVector(const std::string           &blocks,
                            const Responder             &responder,
                            const NetworkPackageStorage &package_storage);
    void timer_sync_tick();
    void process();

    /**
     * @brief remove all blocks
     */
    void removeAllSlot(bool is_mega = false, bool is_exit = false);
};
