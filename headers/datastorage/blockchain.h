#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/genesis_block.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/index/blockindex.h"
#include "datastorage/index/memindex.h"
#include "datastorage/transaction.h"
#include "datastorage/tx_pair.h"
#include "managers/account_controller.h"
#include "utils/bignumber.h"
#include "utils/list_container.h"
#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>

#include <QTemporaryFile>

/*
 * Main database class
 *
 * Is responding for:
 * - saving blocks
 * - validating blocks
 * - merging blocks
 *
 */
class Blockchain : public QObject
{
    const int FIELS_SIZE = 4;
    Q_OBJECT
private:
    // storage //
    bool fileMode;          // true = block storage mode
    ActorIndex *actorIndex; // actors
    BlockIndex blockIndex;  // blocks (if fileMode is true)
    MemIndex memIndex;      // blocks (if fileMode is false)
                            //    Actor<KeyPrivate>   approver;       // current user.
    AccountController *accountController;
    // service //
    int blocksFromLastGenesis = 0;

    bool launched;

public:
    Blockchain(AccountController *accountController, bool fileMode = true);
    ~Blockchain();

private:
    Block getBlockByIndex(const BigNumber &index);
    Block getBlockByApprover(const BigNumber &approver);
    Block getBlockByData(const QByteArray &data);
    Block getBlockByHash(const QByteArray &hash);

    Transaction getTxByHash(const QByteArray &hash, const QByteArray &token = "0");
    Transaction getTxBySender(const BigNumber &id, const QByteArray &token = "0");
    Transaction getTxByReceiver(const BigNumber &id, const QByteArray &token = "0");
    Transaction getTxBySenderOrReceiver(const BigNumber &id, const QByteArray &token = "0");
    Transaction getTxBySenderOrReceiverAndToken(const BigNumber &id, const QByteArray &token = "0");
    Transaction getTxByApprover(const BigNumber &id, const QByteArray &token = "0");
    Transaction getTxByUser(const BigNumber &id, const QByteArray &token = "0");
    TxPair getTxPair(const BigNumber &first, const BigNumber second);

    // genesis blocks //
    bool shouldStartGenesisCreation();
    BigNumber getBalanceFromTx(BigNumber id, Transaction tx);

public:
    void createGenesisBlock();

    QList<Transaction> getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from = -1, int count = 10,
                                                     BigNumber token = 0);

private:
    void addGenesisBlockFromTempFile(const QByteArray &prevGenesisHash);

    // merging //
    int mergeBlockWithLocal(const Block &received);
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

public:
    /**
     * @brief Handy method to read genesis block from temporary file.
     * Delete pointer after using GenesisBlock.
     * @param prevBlock
     * @param prevGenesisHash
     * @return genesis block (unsigned)
     */
    static GenesisBlock *readGenesisBlock(const Block &prevBlock, const QByteArray &prevGenesisHash);

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
     * Gets the transaction from blockchain by *value* of a certain *type*
     * @param value
     * @param type of param
     * @return transaction
     */
    Transaction getTransaction(SearchEnum::TxParam type, const QByteArray &value);

    /**
     * Add block to blockchain
     * Convert block to MemBlock or FileBlock according to a fileMode.
     * @return 0 is success, or error code
     */
    int addBlock(const Block &block, bool isGenesis = false);

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
     * Add actor to actor index
     * @param actor - serialized actor
     * @return 0 is success, or error code
     */
    int addActor(const Actor<KeyPublic> &actor);
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

    BigNumber getUserBalance(BigNumber userId, BigNumber tokenId = BigNumber("0")) const;
    /**
     * @brief Show blockchain
     */
    void showBlockchain() const;

signals:
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

    /**
     * @brief New genesis block is created in temp file TMP_GENESIS_BLOCK
     */
    void GenesisBlockCreated(Block prevBlock, QByteArray prevGenHash);

    // responses
    void TxFound(Transaction tx, SearchEnum::TxParam param, QString value, QHostAddress peerAddress,
                 QByteArray requestHash);

    void TxPairFound(TxPair pair, QHostAddress peerAddress, QByteArray requestHash);
    void BlockFound(Block block, SearchEnum::BlockParam param, QString value, QHostAddress peerAddress,
                    QByteArray requestHash);
    void BlockCount(BigNumber blockCount, QHostAddress peerAddress, QByteArray requestHash);
    void ActorCount(BigNumber actorCount, QHostAddress peerAddress, QByteArray requestHash);

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
    void finished();

public slots:
    void process();

    /**
     * @brief Checks if there is a such block in a local blockchain.
     * Emits BlockExistence or SendMergedBlock signals.
     * @param block
     */
    void checkBlockExistence(const Block &block);

    // from node manager
    void getTxFromBlockchain(SearchEnum::TxParam param, QByteArray value, QHostAddress peerAddress,
                             QByteArray requestHash);

    void getTxPairFromBlockChain(BigNumber sender, BigNumber receiver, QHostAddress peerAddress,
                                 QByteArray requestHash);

    void getBlockFromBlockchain(SearchEnum::BlockParam param, QByteArray value, QHostAddress peerAddress,
                                QByteArray requestHash);
    void getBlockCount(QHostAddress peerAddress, QByteArray requestHash);
    void getActorCount(QHostAddress peerAddress, QByteArray requestHash);

    void addBlockToBlockchain(Block block);

    void newActor(Actor<KeyPublic> actor);

    /**
     * @brief If there no such tx in a previous block
     * adds this tx to the list and emits VerifiedTx signal
     * @param tx
     */
    void VerifyTx(Transaction tx);

    /**
     * @brief finds needed transaction by sender or receiver
     */
    void proveTx();
};
#endif // BLOCKCHAIN_H
