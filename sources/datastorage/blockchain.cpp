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

#include <QJsonObject>

#include "datastorage/blockchain.h"
// #include "datastorage/dfs/dfs_controller.h"
#include "datastorage/index/actorindex.h"
#include "managers/data_mining_manager.h"
#include "managers/transaction_manager.h"

#undef qCritical // temp
#define qCritical qDebug

Blockchain::Blockchain(ExtraChainNode &node)
    : node(node) {
}

Blockchain::~Blockchain() {
}

std::expected<BlockVariant, BlockError>
Blockchain::getBlockByIndex(const BigNumber &index, const bool makeRequestBlock) {
    auto block = blockIndex.getBlockById(index);
    if (!block.has_value())
        return std::unexpected(BlockError::NotExists);
    if (block->isEmpty() && index >= 0 && makeRequestBlock) {
        std::pair<BlockType, BigNumber> requestData(BlockType::Data, index);
        // node.network()->send_message(requestData, MessageType::BlockchainRequestBlock);
        return std::unexpected(BlockError::NotExists);
    }
    return block;
}

// Blocks //

std::expected<BlockVariant, BlockError> Blockchain::getLastBlock() const {
    auto block = blockIndex.getLastBlock();
    return block;
    // return validateAndReturnBlock(block);
}

BigNumber Blockchain::getBlocksStored() const {
    return blockIndex.getLastSavedId() - blockIndex.getFirstSavedId() + 1;
}

std::expected<BlockVariant, BlockError> Blockchain::getLastRealBlock() const {
    auto block = blockIndex.getLastRealBlock();
    return block;
    // return validateAndReturnBlock(block);
}

std::expected<BlockVariant, BlockError> Blockchain::getBlockByData(const QByteArray &data) {
    auto block = blockIndex.getBlockByData(data);
    return block;
    // return validateAndReturnBlock(block);
}

std::expected<BlockVariant, BlockError> Blockchain::getBlockByHash(const QByteArray &hash) {
    auto block = blockIndex.getBlockByHash(hash);
    return block;
    // return validateAndReturnBlock(block);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByHash(const QByteArray &hash, const ActorId &token) {
    return blockIndex.getLastTxByHash(hash, token);
}

void Blockchain::sync() {
    auto lastBlock = getLastBlock();
    if (!lastBlock.has_value()) {
        return;
    }

    auto fromBlock = lastBlock->getIndex();
    if (fromBlock < 0)
        fromBlock = 0;
    qDebug() << "[Blockchain] Request sync from" << lastBlock->getIndex();
    node.network()->send_message(lastBlock->getIndex(), MessageType::BlockchainSync, MessageStatus::Request);
}

void Blockchain::syncResponse(const BigNumber fromBlock, const std::string &messageId) {
    auto lastBlock = getLastBlock();
    if (!lastBlock.has_value()) {
        return;
    }

    BigNumber lastIndex = lastBlock->getIndex();

    if (lastIndex <= fromBlock) {
        // this->sync();
        qDebug() << "[Blockchain] No sync: lastIndex <= fromBlock" << lastIndex << fromBlock;
        return;
    }

    BigNumber from = fromBlock;
    if (from < 0)
        from = 0;

    for (; from <= lastIndex; from++) {
        // qDebug() << "[Blockchain] Send sync" << from;
        auto block = blockIndex.getBlockById(from);

        if (!block.has_value()) {
            continue;
        }

        if (block->isEmpty()) {
            continue;
        }

        if (block->isGenesisBlock()) {
            node.network()->send_message(
                block->getGenesisBlockConst(),
                MessageType::BlockchainGenesisBlock,
                MessageStatus::Response,
                messageId,
                Config::Net::TypeSend::Focused);
        } else {
            node.network()->send_message(
                block->getBlockConst(),
                MessageType::BlockchainNewBlock,
                MessageStatus::Response,
                messageId,
                Config::Net::TypeSend::Focused);
        }
    }

    qDebug() << "[Blockchain] Send for sync: from" << fromBlock << "to" << lastIndex;
}

std::pair<Transaction, QByteArray> Blockchain::getTxBySender(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxBySender(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByReceiver(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxByReceiver(id, token);
}

std::pair<Transaction, QByteArray>
Blockchain::getTxBySenderOrReceiver(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxBySenderOrReceiver(id, token);
}

std::pair<Transaction, QByteArray>
Blockchain::getTxBySenderOrReceiverAndToken(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxBySenderOrReceiverAndToken(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByApprover(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxByApprover(id, token);
}

std::pair<Transaction, QByteArray> Blockchain::getTxByUser(const BigNumber &id, const ActorId &token) {
    return blockIndex.getLastTxByApprover(id, token);
}

BigNumber Blockchain::lastGenesisIdFor(const BigNumber &id) {
    return id / 100 * 100;
}

std::set<Transaction>
Blockchain::getTxsBySenderOrReceiverInRow(const BigNumber &id, BigNumber from, int count, ActorId token) {
    return blockIndex.getTxsBySenderOrReceiverInRow(id, from, count, token);
}

bool Blockchain::sendBlock(const BlockVariant &block) const {
    if (block.isEmpty()) {
        return false;
    }

    if (block.isGenesisBlock()) {
        auto genesisBlock = block.getGenesisBlockConst();
        node.network()->send_message(*genesisBlock, MessageType::BlockchainGenesisBlock);
    } else {
        auto dataBlock = block.getBlockConst();
        node.network()->send_message(*dataBlock, MessageType::BlockchainNewBlock);
    }

    qDebug() << "Send" << block;

    return true;
}

void Blockchain::sendBlockByNumber(const BigNumber &index) const {
    auto block = blockIndex.getBlockById(index);

    if (block.has_value()) {
        return;
    }
    if (block->isEmpty()) {
        return;
    }

    sendBlock(block.value());
}

// Genesis block //

void Blockchain::sendLastGenesisBlock() const {
    const auto genesisBlock = blockIndex.getLastGenesisBlock();
    if (genesisBlock.has_value())
        this->sendBlock(genesisBlock.value());
}

bool Blockchain::isGenesisId(const BigNumber &id) {
    return id == 0 || id % Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS == 0;
}

std::expected<BlockVariant, BlockError>
Blockchain::createGenesisBlock(const std::shared_ptr<Actor<KeyPrivate>> actor) {
    qDebug() << "Creating genesis block";

    if (blockIndex.getLastSavedId().isEmpty() || blockIndex.getRecords() == 0) {
        return std::unexpected(BlockError::EmptyBlockchain);
    }

    GenesisBlock genesis;
    auto         lastBlock        = getLastBlock();
    auto         lastRealBlock    = getLastRealBlock();
    auto         lastGenesisBlock = blockIndex.getLastGenesisBlock();

    if (!lastGenesisBlock.has_value())
        return std::unexpected(BlockError::NoGenesis);
    if (!lastRealBlock.has_value())
        return std::unexpected(BlockError::Unknown);

    genesis.setPrev(lastRealBlock.value());
    genesis.setPrevGen(lastGenesisBlock.value());

    auto lastDataRows = lastGenesisBlock->dataRows();

    for (auto i = lastGenesisBlock->getIndex();
         i != lastGenesisBlock->getIndex() + Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
         i++) {
        auto block = getBlockByIndex(i);

        if (!block.has_value()) {
            continue;
        }
        if (block->isEmpty() || !block->isBlock() || block->getType() != BlockType::Data) {
            continue;
        }

        auto transactions = block->transactions();
        for (auto &transaction : transactions) {
            auto sender   = transaction.getSender();
            auto receiver = transaction.getReceiver();
            auto tokenId  = transaction.getToken();
            lastDataRows[{ sender, tokenId }].state -= transaction.getAmount();
            lastDataRows[{ receiver, tokenId }].state += transaction.getAmount();
        }
    }

    genesis.addRows(lastDataRows);
    genesis.sign(actor);
    return BlockVariant(genesis);
}

std::expected<BlockVariant, BlockError>
Blockchain::createFirstBlock(const std::shared_ptr<Actor<KeyPrivate>> actor) {
    if (blockIndex.getRecords() != 0 || blockIndex.getFirstSavedId() != 0
        || blockIndex.getLastSavedId() != 0) {
        return std::unexpected(BlockError::Invalid);
    }

    GenesisBlock genesis;
    genesis.setIndex(0);
    // genesis.addRows(dataRows);
    genesis.addRow(ActorId(), ActorId(), GenesisDataInfo(0, DataStorage::DataRowType::Universal));
    genesis.addData(actor->id().toStdString());
    genesis.sign(actor);
    return BlockVariant(genesis);
}

// Merging //

std::expected<BlockVariant, BlockError> Blockchain::mergeBlockWithLocal(const BlockVariant &received) {
    const auto existed = getBlockByIndex(received.getIndex());

    if (!existed.has_value()) {
        return std::unexpected(BlockError::CantMerge);
    }

    if (existed->isEmpty() || received.isEmpty()) {
        return std::unexpected(BlockError::CantMerge);
    }

    if (received.getIndex() == 0) {
        return std::unexpected(BlockError::CantMerge);
    }

    if (!canMergeBlocks(received, existed.value())) {
        // qDebug() << "[Blockchain] Blocks with id" << receivedBlockIndex << "can't be merged";
        return std::unexpected(BlockError::CantMerge);
    }

    if (received.isBlock() != existed->isBlock() || received.isGenesisBlock() != existed->isGenesisBlock()) {
        return std::unexpected(BlockError::CantMerge);
    }

    qDebug() << "[Blockchain] Merging block" << received.getIndex();
    if (received == existed && received.signatures() == existed->signatures()) {
        // qDebug() << "[Blockchain] Blocks" << received.getIndex() << "are equal";
        return std::unexpected(BlockError::MergeEqual);
    }

    auto merged = received.isBlock()
                      ? mergeBlocks(received.getBlockConst().value(), existed->getBlockConst().value())
                      : mergeGenesisBlocks(
                            received.getGenesisBlockConst().value(),
                            existed->getGenesisBlockConst().value());

    if (!merged.has_value() || (merged.has_value() && merged->isEmpty())) {
        return std::unexpected(BlockError::CantMerge);
    }

    auto mergedVariant = BlockVariant(merged.value());
    this->replaceBlock(mergedVariant);

    return mergedVariant;
}

std::expected<BlockVariant, BlockError>
Blockchain::getBlock(SearchEnum::BlockParam type, const QByteArray &value) {
    switch (type) {
    case SearchEnum::BlockParam::Id:
        return getBlockByIndex(BigNumber(value.toStdString()));
    case SearchEnum::BlockParam::Data:
        return getBlockByData(value);
    case SearchEnum::BlockParam::Hash:
        return getBlockByHash(value);
    default:
        return std::unexpected(BlockError::Unknown);
    }
}

std::pair<Transaction, QByteArray>
Blockchain::getTransaction(SearchEnum::TxParam type, const QByteArray &value, const ActorId &token) {
    switch (type) {
    case SearchEnum::TxParam::UserSenderOrReceiverOrToken:
        return getTxBySenderOrReceiverAndToken(value.toStdString(), token);
    case SearchEnum::TxParam::Hash:
        return getTxByHash(value, token);
    case SearchEnum::TxParam::User:
        return getTxByUser(value.toStdString(), token);
    case SearchEnum::TxParam::UserApprover:
        return getTxByApprover(value.toStdString(), token);
    case SearchEnum::TxParam::UserReceiver:
        return getTxByReceiver(value.toStdString(), token);
    case SearchEnum::TxParam::UserSender:
        return getTxBySender(value.toStdString(), token);
    case SearchEnum::TxParam::UserSenderOrReceiver:
        return getTxBySenderOrReceiver(value.toStdString(), token);
    default:
        qWarning() << "Can't get tx: incorrect SearchEnum::TxParam. Value:" << value;
        return { Transaction(), "-1" };
    }
}

bool Blockchain::validateBlock(const BlockVariant &block) {
    return node.actorIndex()->validateBlock(block);
}

BlockVariant Blockchain::validateAndReturnBlock(const BlockVariant &block) const {
    // Get prev block hash and check if it exists in current one :)
    return block;
}

void Blockchain::updateFirstId(const BlockVariant &block) {
    if (!block.isGenesisBlock() || block.getIndex() != 0)
        return;

    if (block.dataService().size() > 1 || block.dataService().empty())
        qFatal("Incorrect first genesis");

    auto firstId = ActorId(*block.dataService().begin());
    if (!firstId.isZero())
        node.actorIndex()->setFirstId(firstId);
}

std::expected<BlockVariant, BlockError> Blockchain::addBlock(const BlockVariant &block) {
    if (block.isEmpty())
        return std::unexpected(BlockError::Invalid);

    const auto blockId = block.getIndex();
    if (block.isGenesisBlock() && !Blockchain::isGenesisId(blockId)) {
        qDebug() << "[Blockchain] Incorrect genesis";
        // qFatal("Incorrect genesis");
        return std::unexpected(BlockError::Invalid);
    }

    if (blockId != 0) {
        auto lastBlock = this->getBlockByIndex(blockId - 1);
        auto lastRealBlock = this->getLastRealBlock();

        if (!lastBlock.has_value()) {
            // sync();
            return std::unexpected(BlockError::Invalid);
        }

        if (blockId > lastBlock->getIndex() + 1) {
            qDebug() << "[Blockchain] New block id is greater than last id, sync request";
            sync();
            return std::unexpected(BlockError::Invalid);
        }

        auto expectedPrevHash = block.isGenesisBlock() ? lastRealBlock->getHash() : lastBlock->getHash();
        if (block.getPrevHash() != expectedPrevHash) {            qDebug() << "[Blockchain] Can't chained, sync request";
            qDebug() << "[Blockchain] Can't chained, sync request";
            qDebug() << "jb" << block;
            qDebug() << "lb" << lastBlock.value();
            sync(); // TODO: request only chel who sended block?
            return std::unexpected(BlockError::Invalid);
        }
    }

    if (block.getType() == BlockType::Genesis) {
        // qDebug() << "[Blockchain] Adding a genesis block" << block.getIndex() << "to storage";
    } else {
        // qDebug() << "[Blockchain] Adding a block" << block.getIndex() << "to storage" << block.getType();
    }

    this->updateFirstId(block);

    if (blockId < 0) {
        return std::unexpected(BlockError::Invalid);
    }

    auto newBlock(block);
    signBlock(newBlock);
    auto       res       = blockIndex.addBlock(newBlock);
    const auto blockType = newBlock.getType();

    if (!res.has_value()) {
        if (res.error() == BlockError::AlreadyExists && newBlock.getIndex() > 0) {
            if (newBlock.getType() == BlockType::Dummy) {
                return res;
            }

            auto mergeRes = mergeBlockWithLocal(newBlock);
            return mergeRes;
        }

        return res;
    }

    if (blockType == BlockType::Data) {
        emit updateLastTransactionList();
    }

    qDebug() << "[Blockchain] Block" << blockId << "is added |" << blockType;
    // getSmContractMembers(newBlock);

    if (blockId > 0 && blockId % DFS::Reward::coinProductionAlgorithmTick == 0) {
        node.dataMiningManager()->requestCoinReward();
    }

    return res;
}

std::expected<BlockVariant, BlockError> Blockchain::replaceBlock(const BlockVariant &block) {
    blockIndex.removeById(block.getIndex());
    return addBlock(block);
}

int Blockchain::removeBlock(const BlockVariant &block) {
    return blockIndex.removeById(block);
}

void Blockchain::removeDummyBlocks() {
    blockIndex.removeDummyBlocks();
}

bool Blockchain::canMergeBlocks(const BlockVariant &receivedBlock, const BlockVariant &existedBlock) {
    // 1) Blocks are approved
    // 2) Blocks has one type
    // 3) Blocks ids are identical
    if (receivedBlock.getSignature().empty() || existedBlock.getSignature().empty()
        || receivedBlock.getType() != existedBlock.getType()
        || receivedBlock.getIndex() != existedBlock.getIndex()) {
        return false;
    }

    return true;
}

std::expected<BlockVariant, BlockError> Blockchain::mergeBlocks(const Block &blockA, const Block &blockB) {
    qDebug() << "[Blockchain] Attempting to merge" << blockA << "and" << blockB;

    if (blockA.getIndex() == 0) {
        return std::unexpected(BlockError::CantMerge);
    }

    auto prev = getBlockByIndex(blockA.getIndex() - 1);
    if (!prev.has_value()) {
        return std::unexpected(BlockError::CantMerge);
    }
    if (prev->isEmpty()) {
        return std::unexpected(BlockError::CantMerge);
    }

    const auto &dataServiceA  = blockA.dataService();
    const auto &dataServiceB  = blockB.dataService();
    const auto &transactionsA = blockA.transactions();
    const auto &transactionsB = blockB.transactions();
    const auto &signaturesA   = blockA.signatures();
    const auto &signaturesB   = blockB.signatures();

    bool isDataServiceEqual  = dataServiceA == dataServiceB;
    bool isTransactionsEqual = transactionsA == transactionsB;

    // Case 1 - equal payload
    if (isDataServiceEqual && isTransactionsEqual) {
        Block merged = Block(blockA);
        merged.setPrev(prev.value());

        if (signaturesA != signaturesB) {
            merged.addSignatures(signaturesB);
        }

        BlockVariant mergedVariant = BlockVariant(merged);
        signBlock(mergedVariant);
        return mergedVariant;
    }

    // Case 2 - different payload
    Block merged = Block(blockA);
    merged.clearSignatures();

    if (!isDataServiceEqual)
        merged.addDatas(dataServiceB);

    if (!isTransactionsEqual)
        merged.addTransactions(transactionsB);

    merged.setPrev(prev.value());
    BlockVariant mergedVariant = BlockVariant(merged);
    signBlock(mergedVariant);
    return mergedVariant;
}

std::expected<BlockVariant, BlockError>
Blockchain::mergeGenesisBlocks(const GenesisBlock &blockA, const GenesisBlock &blockB) {
    qDebug() << "[Blockchain] Attempting to merge" << blockA << "and" << blockB;

    if (blockA.getIndex() == 0) {
        return std::unexpected(BlockError::CantMerge);
    }

    auto prev = getBlockByIndex(blockA.getIndex() - 1);
    if (!prev.has_value()) {
        return std::unexpected(BlockError::CantMerge);
    }
    if (prev->isEmpty()) {
        return std::unexpected(BlockError::CantMerge);
    }

    const auto &dataServiceA = blockA.dataService();
    const auto &dataServiceB = blockB.dataService();
    const auto &dataRowsA    = blockA.dataRows();
    const auto &dataRowsB    = blockB.dataRows();
    const auto &signaturesA  = blockA.signatures();
    const auto &signaturesB  = blockB.signatures();

    bool isDataServiceEqual = dataServiceA == dataServiceB;
    bool isDataRowsEqual    = dataRowsA == dataRowsB;

    // Case 1 - equal payload
    if (isDataServiceEqual && isDataRowsEqual) {
        GenesisBlock merged(blockA);

        // merged.setPrevGenHash(blockA.getPrevGenHash());
        merged.setPrev(prev.value());

        if (signaturesA != signaturesB) {
            merged.addSignatures(signaturesB);
        }

        auto mergedVariant = BlockVariant(merged);
        signBlock(mergedVariant);
        return mergedVariant;
    }

    GenesisBlock merged = GenesisBlock(blockA);
    merged.clearSignatures();

    if (!isDataRowsEqual) {
        merged.addRows(dataRowsB);
    }

    if (!isDataServiceEqual) {
        merged.addDatas(dataServiceB);
    }

    merged.setPrev(prev.value());

    BlockVariant mergedVariant = BlockVariant(merged);
    signBlock(mergedVariant);
    return mergedVariant;
}

void Blockchain::signBlock(BlockVariant &block) const {
    block.sign(node.accountController()->currentWallet());
}

BigNumber Blockchain::getRecords() const {
    return blockIndex.getRecords();
}

BigNumber Blockchain::getCountRealBlockRecords() const {
    return blockIndex.getCountRealBlocks();
}

int Blockchain::getCountTransactionsInBlocks() const {
    return blockIndex.getCountTransactionsInBlocks();
}

BigNumberFloat Blockchain::getUserBalance(ActorId userId, TokenId tokenId, TransactionType txType) const {
    BigNumberFloat balance;

    for (BigNumber i = this->blockIndex.getLastSavedId(); i >= blockIndex.getFirstSavedId(); i--) {
        auto currentBlock = blockIndex.getBlockById(i);

        if (!currentBlock.has_value()) {
            continue;
        }
        if (!currentBlock->isBlock() && !currentBlock->isGenesisBlock()) {
            continue;
        }
        if (currentBlock->getType() == BlockType::Dummy) {
            continue;
        }

        if (currentBlock->isGenesisBlock()) {
            auto       genesis = blockIndex.getGenesisBlockById(i);
            const auto rows    = genesis->dataRows();

            for (const auto &[key, row] : rows) {
                if (key.actorId == userId && key.tokenId == tokenId)
                    balance += row.state;
            }

            return balance;
        }

        if (currentBlock->isEmpty())
            break;

        auto txs = currentBlock->transactions();
        for (auto &tx : txs) {
            // if (tx.type() != txType)
            //     continue;

            if (tx.getReceiver() == userId && tx.getToken() == tokenId) {
                balance += tx.getAmount();
            }

            if (tx.getSender() == userId && tx.getToken() == tokenId) {
                balance -= tx.getAmount();
            }
        }
    }

    return balance;
}

void Blockchain::showBlockchain() const {
    qDebug() << "[Blockchain] Show blockchain:";

    auto genBlock = blockIndex.getLastGenesisBlock();
    qDebug() << "Last genesis:" << *genBlock.value().getGenesisBlock();

    int i = 0;

    // do {
    //     auto currentBlock = blockIndex.getBlockById(i);

    //     if (currentBlock->isGenesisBlock())
    //         qDebug() << currentBlock->getGenesisBlockConst();
    //     else
    //         qDebug() << currentBlock->getBlock();

    //     i++;
    // } while (!currentBlock.isEmpty());
}

void Blockchain::getSmContractMembers(const BlockVariant &block) const {
    if (block.isGenesisBlock())
        return;
    auto txList = block.transactions();
    for (const Transaction &tx : txList) {
        if (tx.getData() == "InitContract") {
            node.actorIndex()->getActor(tx.getSender());
            node.actorIndex()->getActor(tx.getReceiver());
        }
    }
}

BigNumber Blockchain::getBlockCount() {
    qDebug() << "BLOCKCHAIN: getBlockCount() count - " << this->blockIndex.getLastSavedId();
    // return this->blockIndex.getRecords(); ?
    return this->blockIndex.getLastSavedId();
}

void Blockchain::addBlockFromNetwork(const BlockVariant &block) {
    if (block.getIndex() > 0 && block.isGenesisBlock()) {
        qDebug() << "!!!!!!!!!!!";
    }

    auto lastBlock = this->getLastBlock();
    if (block.getIndex() != 0 && (!lastBlock.has_value() || (lastBlock.has_value() && block.isEmpty()))) {
        return;
    }

    if (block.getType() != BlockType::Dummy) {
        this->removeDummyBlocks();
    }

    auto res = addBlock(block);

    if (!res.has_value() /*&& res.error() == BlockError::Invalid*/) {
        return;
    }

    // if (res->getType() != BlockType::Dummy) {
        sendBlock(res.value());
    // }
    // sendBlockByNumber(block.getIndex());

    return; // notifications for clients
    auto list = block.transactions();
    for (const auto &tmp : std::as_const(list)) {
        QList<ActorId> list;
        auto           accounts = node.accountController()->accounts();
        for (const auto &tmp : std::as_const(accounts))
            list.append(tmp->id());
        if (list.contains(tmp.getSender())) {
            emit newNotify({ QDateTime::currentMSecsSinceEpoch(),
                             Notification::NotifyType::TxToUser,
                             tmp.getReceiver().toByteArray() });
        } else if (list.contains(tmp.getReceiver())) {
            emit newNotify({ QDateTime::currentMSecsSinceEpoch(),
                             Notification::NotifyType::TxToMe,
                             tmp.getSender().toByteArray() });
        }
    }
}

// Actors //
TransactionProveError Blockchain::proveTransaction(const Transaction &tx) {
    // qDebug() << "[Blockchain] Transaction prove started:" << tx;

    ActorId        targetSender   = tx.getSender();
    ActorId        targetReceiver = tx.getReceiver();
    const ActorId &mainActorId    = node.accountController()->mainActor()->id();
    const ActorId &firstId        = node.actorIndex()->firstId();

    const auto accounts = node.accountController()->accountsIds();
    for (const auto &accountId : accounts) {
        if (accountId == firstId)
            continue;
        if (targetSender == accountId || targetReceiver == accountId) {
            // reward check
            if (tx.isRewardTransaction()) {
                auto approverId = tx.getApprover();
                if (targetSender == ActorId() && !approverId.isZero()) {
                    auto approver = node.actorIndex()->getActor(approverId);
                    bool res      = tx.verify(approver);
                    if (res) {
                        if (tx.getToken() != ActorId()) {
                            return TransactionProveError::RewardWrongToken;
                        }
                        if (tx.getAmount() == 0) {
                            return TransactionProveError::AmountZero;
                        }

                        // continue;
                        return TransactionProveError::NoError;
                    }
                }
            }

            return TransactionProveError::SelfPleasure;
        }
    }

    // reward check
    if (tx.isRewardTransaction()) {
        targetSender = tx.getApprover();
        // TODO: add extended check of validity
        auto res = this->blockIndex.getLastTxByData(tx.getData(), ActorId());

        if (tx.getToken() != ActorId()) {
            return TransactionProveError::RewardWrongToken;
        }

        if (res.second == "-1") {
            return TransactionProveError::NoError;
        }
    }

    Actor<KeyPublic> senderActor;
    if (!targetSender.isZero())
        senderActor = node.actorIndex()->getActor(targetSender);
    Actor<KeyPublic> receiverActor;

    if (!targetReceiver.isZero())
        receiverActor = node.actorIndex()->getActor(targetReceiver);

    if (tx.getAmount() == 0) {
        return TransactionProveError::AmountZero;
    }

    if (tx.getAmount() < 0) {
        return TransactionProveError::AmountLessZero;
    }

    if (targetSender == targetReceiver) {
        return TransactionProveError::IdenticalSenderReceiver;
    }

    auto block = getLastRealBlock();
    if (!block.has_value() && block->isEmpty()) {
        return TransactionProveError::EmptyBlockchain;
    }

    // if receiver is not exist
    if (senderActor.empty() && !targetSender.isZero()) {
        return TransactionProveError::SenderNotExists;
    }

    if (receiverActor.empty() && !targetReceiver.isZero()) {
        return TransactionProveError::ReceiverNotExists;
    }

    if (tx.getSignature().empty()) {
        return TransactionProveError::MissingSignature;
    }

    // special conditions: receiver is null - coins burning
    if (targetSender.isZero()) {
        Actor<KeyPublic> producerActor;
        if (!tx.getProducer().isZero())
            producerActor = node.actorIndex()->getActor(tx.getProducer());
        else {
            // return TransactionProveError::ZeroProducer;
        }

        if (!producerActor.key().verify(tx.getHash(), tx.getSignature())) {
            // return TransactionProveError::ProducerVerify;
        }

        return TransactionProveError::NoError;
    }

    //    // if !sig
    //    if (!senderActor.key().verify(tx->getDataForSignature().toStdString(),
    //    tx->getSignature().toStdString()))
    //    {
    //        qDebug() << "Tx" << tx->getHash() << "not approved: bad signature";
    //        return TransactionProveError::InvalidSignature;
    //    }

    // special conditions: receiver is null - coins burning, contract creation
    if (targetReceiver.isZero()) {
        qDebug() << "target received is empty";

        Transaction provedTx(tx);
        provedTx.sign(node.accountController()->currentWallet());
        return TransactionProveError::NoError;
    } else {
        if (tx.getData() == "InitContract") {
            return TransactionProveError::Unknown;
        }

        if (targetSender != firstId) {
            BigNumberFloat senderCurrentBalance = getUserBalance(targetSender, tx.getToken());
            senderCurrentBalance += node.transactionManager()->checkPendingTxsList(targetSender);

            BigNumberFloat transactionAmount = tx.getAmount();
            BigNumberFloat transactionFee    = 0; // transactionAmount / 100;
            BigNumberFloat senderNewBalance  = senderCurrentBalance - transactionAmount - transactionFee;

            if (senderNewBalance < 0 /* && mainActorId == firstId */) {
                return TransactionProveError::SenderBalanceBelowZero;
            }

            // sign?
            return TransactionProveError::NoError;
        } else {
            Transaction provedTx(tx);
            provedTx.sign(node.accountController()->currentWallet());
            return TransactionProveError::NoError;
        }

        return TransactionProveError::Unknown;
    }

    return TransactionProveError::Unknown;
}

void Blockchain::process() {
}

// Other //

BlockIndex &Blockchain::getBlockIndex() {
    return blockIndex;
}

void Blockchain::removeAll() {
    this->blockIndex.removeAll();
    QFile(DataStorage::TMP_GENESIS_BLOCK).remove();
}
