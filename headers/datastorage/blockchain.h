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
#include "datastorage/index/actorindex.h"
#include "datastorage/index/blockindex.h"
#include "datastorage/index/memindex.h"
#include "datastorage/transaction.h"
#include "managers/account_controller.h"
#include "utils/bignumber.h"
#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QMutex>
#include <QTemporaryFile>
#include <cassert>
// database
#include "headers/utils/db_connector.h"
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
static QMutex mutex;
class Blockchain : public QObject
{
    //    static_assert(is_same<T, Block>::value || is_same<T, GenesisBlock>::value,
    //                  "Your type is not supported."
    //                  "Supportable types: BigNumber, Transaction, Block, TxPair, Actor");
    Q_OBJECT
private:
    // storage //
    bool fileMode;          // true = block storage mode
    ActorIndex *actorIndex; // actors
    BlockIndex blockIndex;  // blocks (if fileMode is true)
    MemIndex memIndex;      // blocks (if fileMode is false)
                            //    Actor<KeyPrivate>   approver;       // current user.
    AccountController *accountController;
    TransactionManager *txManager;
    // service //
    QList<GenesisDataRow> genBlockData; // actorid -> token
    int blocksFromLastGenesis = 0;

    bool launched;

public:
    Blockchain(AccountController *accountController, bool fileMode = true);
    Block getBlockByHash(const QByteArray &hash);
    ~Blockchain();

private:
    Block getBlockByIndex(const BigNumber &index);
    Block getBlockByApprover(const BigNumber &approver);
    Block getBlockByData(const QByteArray &data);

    QByteArray getBlockDataByIndex(const BigNumber &index);

    std::pair<Transaction, QByteArray> getTxByHash(const QByteArray &hash, const QByteArray &token = "0");
    std::pair<Transaction, QByteArray> getTxBySender(const BigNumber &id, const QByteArray &token = "0");
    std::pair<Transaction, QByteArray> getTxByReceiver(const BigNumber &id, const QByteArray &token = "0");
    std::pair<Transaction, QByteArray> getTxBySenderOrReceiver(const BigNumber &id,
                                                               const QByteArray &token = "0");
    std::pair<Transaction, QByteArray> getTxBySenderOrReceiverAndToken(const BigNumber &id,
                                                                       const QByteArray &token = "0");
    std::pair<Transaction, QByteArray> getTxByApprover(const BigNumber &id, const QByteArray &token = "0");
    std::pair<Transaction, QByteArray> getTxByUser(const BigNumber &id, const QByteArray &token = "0");

    void saveTxInfoInEC(const QByteArray data) const;

    // genesis blocks //
    bool shouldStartGenesisCreation();

    void addRecordsIfNew(const GenesisDataRow &row1, const GenesisDataRow &row2);
    QByteArray findRecordsInBlock(const Block &block);
    bool signCheckAdd(Block &block);
    void sendFeeUnfreeze(Block &block);
    void sendUnFee(Block &block);
    QMap<QByteArray, BigNumber> getInvestmentsStaking(const BigNumber &wallet, const BigNumber &token);

    const int COUNT_APPROVER_BLOCK = 1;
    const int COUNT_CHECKER_BLOCK = 2;
    const int COUNT_UNFROZE_FEE = 3;
    const BigNumber StakingCoef = 5;

public:
    GenesisBlock createGenesisBlock(const Actor<KeyPrivate> actor,
                                    QMap<BigNumber, BigNumber> states = QMap<BigNumber, BigNumber>());

    QList<Transaction> getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from = -1, int count = 10,
                                                     BigNumber token = 0);
    void getBlockZero();
    BigNumber getSupply(const QByteArray &idToken);
    BigNumber getFullSupply(const QByteArray &idToken);

    bool checkHaveUNFreezeTx(const Transaction *tx, const BigNumber &indexBlock); // return true if haven`t
private:
    void addGenesisBlockFromTempFile(const QByteArray &prevGenesisHash);
    Block checkBlock(const Block &block);
    // merging //
    int mergeBlockWithLocal(Block &received);
    int mergeGenesisBlockWithLocal(const GenesisBlock &received);

    /**
     * @brief validates block digital signature
     * @param block
     * @return true if block is valid
     */
    bool validateBlock(const Block &block);
    /**
     * @brief validates block using validateBlock method
     * @param block
     * @return block - if it is valid, empty block - if block is corrupted.
     */
    Block validateAndReturnBlock(const Block &block);
    void stakingReward(const Block &block);

    std::pair<BigNumber, BigNumber> getLastTxForStaking(const BigNumber &receiver, const BigNumber &token);

    bool checkStakingReward(const QByteArray &hash, const BigNumber &token, const BigNumber receiver);

public:
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
    Block getLastBlock();
    /**
     * Gets the block from blockchain by *value* of a certain *type*
     * @param value
     * @param type of param
     * @return last blockchain block
     */
    Block getBlock(SearchEnum::BlockParam type, const QByteArray &value);
    /**
     * Gets the block from blockchain by *value* of a certain *type*
     * @param value
     * @param type of param
     * @return last blockchain block
     */
    QByteArray getBlockData(SearchEnum::BlockParam type, const QByteArray &value);
    /**
     * Gets the transaction from blockchain by *value* of a certain *type*
     * @param value
     * @param type of param
     * @return transaction
     */
    std::pair<Transaction, QByteArray> getTransaction(SearchEnum::TxParam type, const QByteArray &value,
                                                      const QByteArray &token = "0");

    /**
     * Add block to blockchain
     * Convert block to MemBlock or FileBlock according to a fileMode.
     * @return 0 is success, or error code
     */
    int addBlock(Block &block, bool isGenesis = false);

    /**
     * Removes block and all blocks after them
     * @return 0 is success, or error code
     */
    int removeBlock(const Block &block);

    /**
     * @brief Check if two blocks can be merged
     * (has identical id and at least one common transaction)
     * @param blockA
     * @param blockB
     * @return true, if blocks can be merged
     */
    bool canMergeBlocks(const Block &blockA, const Block &blockB);
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
     * @param block with digSig
     */
    void signBlock(Block &block) const;

    // - ACTORS - //

    /**
     * Gets actor from actor index
     * @param actorId
     * @return actor
     */
    Actor<KeyPublic> getActor(const BigNumber &actorId);
    /**
     * @brief remove all blocks
     */
    void removeAll();
    /**
     * @brief getApprover
     * @return
     */
    Actor<KeyPrivate> getApprover() const;
    /**
     * @brief setApprover
     * @param value
     */
    void setApprover(const Actor<KeyPrivate> &value);
    /**
     * @brief true - file mode, false - memory mode
     * @param memory
     */
    void setMode(bool fileMode);
    /**
     * @brief Return's reference to actorIndex
     * @return ref to actorIndex field
     */
    ActorIndex *getActorIndex();

    /**
     * @brief Return's reference to memIndex
     * @return ref to memIndex field
     */
    MemIndex &getMemIndex();

    /**
     * @brief Return's reference to blockIndex
     * @return ref to blockIndex field
     */
    BlockIndex &getBlockIndex();

    /**
     * @brief Gets block count in a local storage
     * @return block count
     */
    BigNumber getBlockChainLength() const;
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

    BigNumber getUserBalance(BigNumber userId, BigNumber tokenId) const;
    BigNumber getFreezeUserBalance(BigNumber userId, BigNumber tokenId, BigNumber sender = -1) const;

    QMap<QByteArray, BigNumber> getAllStakingForMe(BigNumber userId, BigNumber tokenId) const;
    /**
     * @brief Show blockchain
     */
    void showBlockchain() const;

    bool isSmContractTx(const Block &block) const;

    void getSmContractMembers(const Block &block) const;
signals:

    void newNotify(const Notification ntf);
    void addActorInActorIndex(Actor<KeyPublic> actor);
    void updateTransactionListInModel(QByteArray, QByteArray);
    /**
     * @brief Sends new verified block to the network. Should be emited when
     * merged is created
     * @param firstBlock
     * @param secondBlock
     * @param resultBlock - merged block
     */
    //    void SendMergedBlock(Block firstBlock, Block secondBlock, Block
    //    resultBlock);
    /**
     * @brief Block is corrupted (validation is not passed)
     * @param block
     */
    void BlockCorrupted(Block block);
    /**
     * @brief New block created
     * @param block
     */
    void NewBlock(Block block);

    // responses
    void responseReady(const QByteArray &data, const unsigned int &msgType, const QByteArray &requestHash,
                       const SocketPair &receiver);

    /**
     * @brief There no such block in a local blockchain
     * @param block
     */
    void BlockIsMissing(Block block);

    /**
     * @brief Transaction is verified by blockchain
     * @param tx - verified transaction
     */
    void VerifiedTx(Transaction tx);

    void updateLastTransactionList();
    void sendMessage(const QByteArray &data, const unsigned int &type);
    void finished();

public:
    void addBlockToBlockchain(Block block);
    void addGenBlockToBlockchain(GenesisBlock block);
    void setTxManager(TransactionManager *value);

public slots:

    void process();
    void updateBlockchain(BigNumber id, bool isUser);
    void updateBlockchainForSignIn(QByteArray id, QByteArrayList idList);
    /**
     * @brief Checks if there is a such block in a local blockchain.
     * Emits BlockExistence or SendMergedBlock signals.
     * @param block
     */
    void checkBlockExistence(Block &block);
    /**
     * @brief blockCountResponse
     * @param count
     */
    void blockCountResponse(const BigNumber &count);
    // from node manager
    void getTxFromBlockchain(const SearchEnum::TxParam &param, const QByteArray &value,
                             const SocketPair &receiver, const QByteArray &request);

    void getBlockFromBlockchain(const SearchEnum::BlockParam &param, const QByteArray &value,
                                const QByteArray &requestHash, const SocketPair &receiver);
    void getBlockCount(const QByteArray &requestHash, const SocketPair &receiver);

    /**
     * @brief If there no such tx in a previous block
     * adds this tx to the list and emits VerifiedTx signal
     * @param tx
     */
    void VerifyTx(Transaction tx);

    /**
     * @brief finds needed transaction by sender or receiver
     */
    void proveTx(Transaction *tx);
};
#endif // BLOCKCHAIN_H
