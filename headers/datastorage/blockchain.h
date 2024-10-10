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

#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/genesis_block.h"
#include "datastorage/index/blockindex.h"
#include "datastorage/transaction.h"
#include "utils/dfs_utils.h"
#include "utils/bignumber.h"

#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>
#include <cassert>

class TransactionManager;

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

class EXTRACHAIN_EXPORT Blockchain : public QObject {
    //    static_assert(is_same<T, Block>::value || is_same<T, GenesisBlock>::value,
    //                  "Your type is not supported."
    //                  "Supportable types: BigNumber, Transaction, Block, TxPair, Actor");
    Q_OBJECT

private:
    ExtraChainNode &node;

    // storage //
    BlockIndex blockIndex; // blocks (if fileMode is true)
                           //    Actor<KeyPrivate>   approver;       // current user.
    // service //

public:
    explicit Blockchain(ExtraChainNode &node);
    std::expected<BlockVariant, BlockError> getBlockByHash(const QByteArray &hash);
    ~Blockchain();

    std::expected<BlockVariant, BlockError>
    getBlockByIndex(const BigNumber &index, const bool makeRequestBlock = false);
    std::pair<Transaction, QByteArray> getTxByHash(const QByteArray &hash, const ActorId &token = ActorId());

    void sync();
    void syncResponse(const BigNumber fromBlock, const std::string &messageId);

private:
    std::expected<BlockVariant, BlockError> getBlockByData(const QByteArray &data);

    std::pair<Transaction, QByteArray> getTxBySender(const BigNumber &id, const ActorId &token = ActorId());
    std::pair<Transaction, QByteArray> getTxByReceiver(const BigNumber &id, const ActorId &token = ActorId());
    std::pair<Transaction, QByteArray>
    getTxBySenderOrReceiver(const BigNumber &id, const ActorId &token = ActorId());
    std::pair<Transaction, QByteArray>
    getTxBySenderOrReceiverAndToken(const BigNumber &id, const ActorId &token = ActorId());
    std::pair<Transaction, QByteArray> getTxByApprover(const BigNumber &id, const ActorId &token = ActorId());
    std::pair<Transaction, QByteArray> getTxByUser(const BigNumber &id, const ActorId &token = ActorId());

    // genesis blocks //
    QByteArray                  findRecordsInBlock(const BlockVariant &block);
    bool                        signCheckAdd(BlockVariant &block);
    QMap<QByteArray, BigNumber> getInvestmentsStaking(const ActorId &wallet, const ActorId &token);

    const int       COUNT_APPROVER_BLOCK = 1;
    const int       COUNT_CHECKER_BLOCK  = 2;
    const int       COUNT_UNFROZE_FEE    = 3;
    const BigNumber StakingCoef          = 5;

public:
    static BigNumber lastGenesisIdFor(const BigNumber &id);
    static bool      isGenesisId(const BigNumber &id);

    std::expected<BlockVariant, BlockError>
    createGenesisBlock(const std::shared_ptr<Actor<KeyPrivate>> actor);

    std::expected<BlockVariant, BlockError>
    createFirstBlock(const std::shared_ptr<Actor<KeyPrivate>>
                         actor /*, std::map<std::pair<ActorId, TokenId>, GenesisDataRow> dataRows = {}*/);

    std::set<Transaction> getTxsBySenderOrReceiverInRow(
        const BigNumber &id,
        BigNumber        from  = -1,
        int              count = 10,
        ActorId          token = ActorId());
    // BigNumber getSupply(const QByteArray &idToken);
    // BigNumber getFullSupply(const QByteArray &idToken);
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
    /**
     * @brief calculate reward amound of income reward request
     * @param requestReward - request reward data
     * @return amount of reward
     */
    BigNumberFloat calculateRewardAmount(const DFS::Reward::RequestReward &requestReward) const;

    /**
     *
     */
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
    std::expected<BlockVariant, BlockError> getBlock(SearchEnum::BlockParam type, const QByteArray &value);

    /**
     * Gets the transaction from blockchain by *value* of a certain *type*
     * @param value
     * @param type of param
     * @return transaction
     */
    std::pair<Transaction, QByteArray>
    getTransaction(SearchEnum::TxParam type, const QByteArray &value, const ActorId &token = ActorId());

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
    void removeAllDummyBlocks(const BlockVariant &block);

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
    std::expected<BlockVariant, BlockError>
    mergeGenesisBlocks(const GenesisBlock &blockA, const GenesisBlock &blockB);

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
     * @brief true - file mode, false - memory mode
     * @param memory
     */
    void setMode(bool fileMode);

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

    BigNumberFloat getUserBalance(
        ActorId         userId,
        TokenId         tokenId = TokenId(),
        TransactionType txType  = TransactionType::Transaction) const;

    /**
     * @brief Show blockchain
     */
    void showBlockchain() const;

    void getSmContractMembers(const BlockVariant &block) const;

signals:
    void finished();
    void newNotify(Notification ntf);
    void updateLastTransactionList();

    /**
     * @brief possibleMiningChange
     * @param possibleMinig
     */
    void possibleMiningChange(const bool &possibleMinig);

public:
    void      addBlockFromNetwork(const BlockVariant &block);
    BigNumber getBlockCount();

    /**
     * @brief finds needed transaction by sender or receiver
     */
    TransactionProveError proveTransaction(const Transaction &tx);

public slots:
    void process();
};

#endif // BLOCKCHAIN_H
