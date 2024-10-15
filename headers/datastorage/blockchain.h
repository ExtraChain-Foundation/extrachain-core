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
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "network/message_body.h"
#include "network/network_manager.h"
#include "utils/bignumber.h"
#include <QByteArray>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTemporaryFile>
#include <QtNetwork/QHostAddress>
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

class EXTRACHAIN_EXPORT Blockchain : public QObject {
    //    static_assert(is_same<T, Block>::value || is_same<T, GenesisBlock>::value,
    //                  "Your type is not supported."
    //                  "Supportable types: BigNumber, Transaction, Block, TxPair, Actor");
    Q_OBJECT

private:
    ExtraChainNode *node;

    // storage //
    BlockIndex blockIndex; // blocks (if fileMode is true)
                           //    Actor<KeyPrivate>   approver;       // current user.
    // service //
    std::vector<GenesisDataRow> genBlockData; // actorid -> token

    bool launched;
    BigNumber circulativeSupply;
    bool possibleMining = true;

public:
    explicit Blockchain(ExtraChainNode *node);
    BlockVariant getBlockByHash(const QByteArray &hash);
    ~Blockchain();

    BlockVariant getBlockByIndex(const BigNumber &index, const bool makeRequestBlock = false);
    std::pair<Transaction, QByteArray> getTxByHash(const QByteArray &hash, const ActorId &token = ActorId());

    void sync();
    void syncResponse(const BigNumber fromBlock, const std::string &messageId);

private:
    BlockVariant getBlockByData(const QByteArray &data);

    std::string getBlockDataByIndex(const BigNumber &index);

    std::pair<Transaction, QByteArray> getTxBySender(const BigNumber &id, const ActorId &token = ActorId());
    std::pair<Transaction, QByteArray> getTxByReceiver(const BigNumber &id, const ActorId &token = ActorId());
    std::pair<Transaction, QByteArray>
    getTxBySenderOrReceiver(const BigNumber &id, const ActorId &token = ActorId());
    std::pair<Transaction, QByteArray>
    getTxBySenderOrReceiverAndToken(const BigNumber &id, const ActorId &token = ActorId());
    std::pair<Transaction, QByteArray> getTxByApprover(const BigNumber &id, const ActorId &token = ActorId());
    std::pair<Transaction, QByteArray> getTxByUser(const BigNumber &id, const ActorId &token = ActorId());

    void saveTxInfoInEC(const std::set<Transaction> &transactions) const;

    // genesis blocks //
    bool shouldStartGenesisCreation();

    void addRecordsIfNew(const GenesisDataRow &row1, const GenesisDataRow &row2);
    QByteArray findRecordsInBlock(const BlockVariant &block);
    bool signCheckAdd(BlockVariant &block);
    QMap<QByteArray, BigNumber> getInvestmentsStaking(const ActorId &wallet, const ActorId &token);

    const int COUNT_APPROVER_BLOCK = 1;
    const int COUNT_CHECKER_BLOCK = 2;
    const int COUNT_UNFROZE_FEE = 3;
    const BigNumber StakingCoef = 5;

public:
    GenesisBlock createGenesisBlock(
        const std::shared_ptr<Actor<KeyPrivate>> actor,
        QMap<ActorId, BigNumberFloat> states = QMap<ActorId, BigNumberFloat>());

    std::set<Transaction> getTxsBySenderOrReceiverInRow(const BigNumber &id,
                                                        BigNumber from = -1,
                                                        int count = 10,
                                                        ActorId token = ActorId());
    void getBlockZero();
    BigNumber getSupply(const QByteArray &idToken);
    BigNumber getFullSupply(const QByteArray &idToken);
    void sendBlockByNumber(const BigNumber &index) const;
    void sendLastGenesisBlock() const;

private:
    // merging //
    int mergeBlockWithLocal(BlockVariant &received);
    int mergeGenesisBlockWithLocal(const GenesisBlock &received);

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

    /**
     * Compares prevHash field of every block
     * with the hash of the prev block
     * @return 0 if integrity is ok, or block id where integrity is corrupted
     */
    BigNumber checkIntegrity();

    // - BLOCKS - //

    /**
     * @return last blockchain block
     */
    BlockVariant getLastBlock() const;

    /**
     * @return Amount of blockchain blocks
     */
    BigNumber getBlocksStored() const;

    /**
     * @return last real blockchain block
     */
    BlockVariant getLastRealBlock() const;

    /**
     * Gets the block from blockchain by *value* of a certain *type*
     * @param value
     * @param type of param
     * @return last blockchain block
     */
    BlockVariant getBlock(SearchEnum::BlockParam type, const QByteArray &value);

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
    int addBlock(const BlockVariant &block);

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
    Block mergeBlocks(const Block &blockA, const Block &blockB);

    GenesisBlock mergeGenesisBlocks(const GenesisBlock &blockA, const GenesisBlock &blockB);

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

    BigNumberFloat
    getUserBalance(ActorId userId, ActorId tokenId = ActorId(), TransactionType txType = TransactionType::Transaction) const;

    /**
     * @brief Show blockchain
     */
    void showBlockchain() const;

    void getSmContractMembers(const BlockVariant &block) const;

    /**
     *  @brief Get circulative supply
     */
    BigNumber getCirculativeSuply() const;

    /**
     * @brief Set new value circulative supply
     */
    void setCirculativeSupply(const BigNumber &newValue);

    /**
     * @brief Increase circulative supply value
     */
    void increaseCirculativeSupply(const BigNumber &value);

    /**
     * @brief Set possible mining
     */
    void setPossibleMining(const bool &value);

    /**
     * @brief Get possible mining
     */
    bool getPossibleMining() const;

signals:
    void newNotify(Notification ntf);
    void updateLastTransactionList();

    /**
     * @brief possibleMiningChange
     * @param possibleMinig
     */
    void possibleMiningChange(const bool &possibleMinig);

public:
    void addBlockFromNetwork(const BlockVariant &block);
    void addGenesisBlockFromNetwork(const GenesisBlock &block);
    BigNumber getBlockCount();

    /**
     * @brief finds needed transaction by sender or receiver
     */
    TransactionProveError proveTransaction(const Transaction &tx);
};

#endif // BLOCKCHAIN_H
