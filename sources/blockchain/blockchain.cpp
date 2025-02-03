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

#include <QJsonObject>

#include "blockchain/blockchain.h"
// #include "dfs/dfs_controller.h"
#include "blockchain/actor_index.h"
#include "managers/data_mining_manager.h"
#include "managers/transaction_manager.h"
#include "network/network_manager.h"

Blockchain::Blockchain(ExtraChainNode *node)
    : /*QObject(node)
    , */
    node(node) {

    // get first id on start
    auto genesis = blockIndex.getGenesisBlockById(BigNumber(0));
    if (genesis.has_value()) {
        updateFirstId(genesis.value());
    }
}

Blockchain::~Blockchain() {
}

std::expected<BlockVariant, BlockError> Blockchain::getBlockByIndex(const BigNumber &index,
                                                                    const bool       makeRequestBlock) {
    auto block = blockIndex.getBlockById(index);
    if (!block.has_value())
        return std::unexpected(BlockError::NotExists);
    if (block->isEmpty() && index >= 0 && makeRequestBlock) {
        std::pair<BlockType, BigNumber> requestData(BlockType::Data, index);
        // node->network()->send_message(requestData, MessageType::BlockchainRequestBlock);
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

std::expected<BlockVariant, BlockError> Blockchain::getBlockByData(const std::string &data) {
    auto block = blockIndex.getBlockByData(data);
    return block;
    // return validateAndReturnBlock(block);
}

std::expected<BlockVariant, BlockError> Blockchain::getBlockByHash(const std::string &hash) {
    auto block = blockIndex.getBlockByHash(hash);
    return block;
    // return validateAndReturnBlock(block);
}

std::pair<Transaction, BigNumber> Blockchain::getTxByHash(const std::string &hash, const ActorId &token) {
    return blockIndex.getLastTxByHash(hash, token);
}

void Blockchain::sync(const BigNumber &from, std::optional<Responder> responder) {
    auto lastBlock = getLastBlock();
    auto fromBlock = lastBlock.has_value() ? lastBlock->getIndex() : from;
    if (fromBlock < 0)
        fromBlock = 0;
    // eLog("[Blockchain] Request sync from {}", fromBlock);

    if (!responder.has_value()) {
        eFatal("[Blockchain] all parent sync");
        node->network()->send_message(fromBlock,
                                      MessageType::BlockchainSync,
                                      SendMode::Neighbours,
                                      MessageStatus::Request);
    } else {
        node->network()->send_message(fromBlock,
                                      MessageType::BlockchainSync,
                                      SendMode::Focused,
                                      MessageStatus::Request,
                                      responder.value());
    }
}

void Blockchain::syncResponse(const BigNumber fromBlock, const Responder &responder) {
    auto lastBlock = getLastBlock();
    if (!lastBlock.has_value()) {
        return;
    }

    BigNumber lastIndex = lastBlock->getIndex();

    if (lastIndex < fromBlock) {
        // this->sync();
        // eLog("{} {}", lastIndex, fromBlock);
        return;
    }

    BigNumber from = fromBlock;
    if (from < 0)
        from = 0;

    std::vector<BlockVariant> blocks;
    blocks.reserve(1000);

    for (; from <= lastIndex; from++) {
        // eLog("[Blockchain] Send sync {}", from);
        auto block = blockIndex.getBlockById(from);

        if (!block.has_value()) {
            continue;
        }

        if (block->isEmpty()) {
            continue;
        }

        blocks.push_back(block.value());

        if (blocks.size() > 1000) {
            responder.send_response(blocks,
                                    MessageType::BlockchainSyncBlocks,
                                    SendMode::Focused,
                                    MessageStatus::Response);
            blocks.clear();
        }

        // if (block->isGenesisBlock()) {
        //     node->network()->send_message(block->getGenesisBlockConst(),
        //                                   MessageType::BlockchainGenesisBlock,
        //                                   SendMode::Focused,
        //                                   MessageStatus::Response,
        //                                   "",
        //                                   identfier);
        // } else {
        //     node->network()->send_message(block->getBlockConst(),
        //                                   MessageType::BlockchainNewBlock,
        //                                   SendMode::Focused,
        //                                   MessageStatus::Response,
        //                                   "",
        //                                   identfier);
        // }
    }

    if (blocks.empty()) {
        return;
    }

    responder.send_response(blocks, MessageType::BlockchainSyncBlocks, SendMode::Focused, MessageStatus::Response);

    // eLog("[Blockchain] Send for sync: from {} to {}", fromBlock, lastIndex);
}

void Blockchain::syncResponseVector(std::vector<BlockVariant>    blocks,
                                    const Responder             &responder,
                                    const NetworkPackageStorage &package_storage) {
    if (blocks.empty()) {
        eLog("[Blockchain] Sync: incoming empty blocks... Why?");
    }

    eLog("[Blockchain] Sync: incomining {} blocks... First: {}, last: {}",
         blocks.size(),
         blocks.front().getIndex(),
         blocks.back().getIndex());

    status_ = BlockchainStatus::Maybe;
    for (const auto &block : blocks) {
        addBlockNetwork(block, responder, package_storage, false);
    }
    start_check();
}

void Blockchain::lastSavedRequest() {
    // node->network()->send_message(0, MessageType::BlockchainLastSaved, MessageStatus::Request);
}

BlockchainStatus Blockchain::status() {
    return status_;
}

void Blockchain::start_sync() {
    // start timer, after end -> again request
    if (status_ == BlockchainStatus::Sync) {
        eLog("BC 11 start_sync return");
        return;
    }

    timer_sync->stop();
    timer_sync->start(10000);

    if (status_ != BlockchainStatus::Sync) {
        status_ = BlockchainStatus::Sync;
        emit statusChanged(status_);
    }

    last_info_.clear();
    sync_status_   = BlockchainSyncStatus::LastInfo;
    requests_count = node->network()->active_connections_count();
    node->network()->send_message(true,
                                  MessageType::BlockchainSyncLastInfo,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);

    eLog("BC 10 start_sync");
}

void Blockchain::start_check() {
    if (status_ != BlockchainStatus::Ready || status_ == BlockchainStatus::Maybe) {
        start_sync();
        eLog("BC 12 start_check return");
        return;
    }

    last_info_.clear();
    check_status_  = BlockchainSyncStatus::LastInfo;
    requests_count = node->network()->active_connections_count();
    node->network()->send_message(true,
                                  MessageType::BlockchainSyncLastInfo,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);

    eLog("BC 9 start_check");
}

void Blockchain::network_status_sync_request(const Responder &responder) {
    auto        block     = this->getLastRealBlock();
    BigNumber   block_id  = block.has_value() ? block->getIndex() : BigNumber(-1);
    std::string hash      = block.has_value() ? block->getHash() : "";
    auto        last_info = BlockchainLastInfo { .last_block_id = block_id, .last_hash = hash };
    eLog("network_status_sync_request, send: {}", last_info);
    responder.send_response(last_info,
                            MessageType::BlockchainSyncLastInfo,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void Blockchain::network_status_sync_response(const BlockchainLastInfo &last_info, const Responder &responder) {
    if (sync_status_ != BlockchainSyncStatus::LastInfo && check_status_ != BlockchainSyncStatus::LastInfo) {
        return;
    }
    // min(connections size, 5)

    int count = std::min(requests_count, 5);

    last_info_.insert({ *responder.identifiers().begin(), last_info });

    if (sync_status_ == BlockchainSyncStatus::LastInfo && last_info_.size() >= count) {
        sync_status_  = BlockchainSyncStatus::Blocks;
        check_status_ = BlockchainSyncStatus::None;
        eLog("BC 6 sync status");
        send_request_blocks();
    }

    if (check_status_ == BlockchainSyncStatus::LastInfo && last_info_.size() >= count) {
        check_status_ = BlockchainSyncStatus::Blocks;
        eLog("BC 7 check status");
        send_request_blocks();
    }
}

void Blockchain::send_request_blocks() {
    auto block = this->getLastRealBlock();

    if (last_info_.empty()) {
        eLog("BC 5");
        return;
    }

    // Проверяем, нужна ли синхронизация
    bool need_sync = false;

    if (!block.has_value()) {
        // Если у нас пустой блокчейн - проверяем есть ли ноды с непустым
        for (const auto &[_, info] : last_info_) {
            if (info.last_block_id >= 0 && !info.last_hash.empty()) {
                need_sync = true;
                break;
            }
        }
    } else {
        const auto my_index = block->getIndex();
        const auto my_hash  = block->getHash();

        for (const auto &[_, info] : last_info_) {
            if (info.last_block_id > my_index) {
                need_sync = true;
                break;
            }
            if (info.last_block_id == my_index && info.last_hash != my_hash) {
                need_sync = true;
                blockIndex.removeById(my_index);
                break;
            }
        }
    }

    if (!need_sync) {
        sync_status_  = BlockchainSyncStatus::None;
        check_status_ = BlockchainSyncStatus::None;
        status_       = BlockchainStatus::Ready;
        emit statusChanged(status_);
        timer_sync->stop();

        eLog("BC 4");
        return; // end sync
    }

    int connections = requests_count;
    int max_nodes   = std::min(connections, 5);

    std::vector<std::pair<std::string, BigNumber>> nodes_by_block;
    for (const auto &[id, info] : last_info_) {
        // Добавляем только ноды с непустым блокчейном
        if (info.last_block_id >= 0 && !info.last_hash.empty()) {
            nodes_by_block.emplace_back(id, info.last_block_id);
        }
    }

    // Если нет нод с непустым блокчейном - выходим
    if (nodes_by_block.empty()) {
        eLog("BC 3");
        sync_status_  = BlockchainSyncStatus::None;
        check_status_ = BlockchainSyncStatus::None;
        status_       = BlockchainStatus::Ready;
        emit statusChanged(status_);
        timer_sync->stop();

        return;
    }

    if (nodes_by_block.size() > max_nodes) {
        std::partial_sort(nodes_by_block.begin(),
                          nodes_by_block.begin() + max_nodes,
                          nodes_by_block.end(),
                          [](const auto &a, const auto &b) {
                              return a.second > b.second;
                          });
        nodes_by_block.resize(max_nodes);
    } else {
        std::sort(nodes_by_block.begin(), nodes_by_block.end(), [](const auto &a, const auto &b) {
            return a.second > b.second;
        });
    }

    if (sync_status_ != BlockchainSyncStatus::Blocks) {
        start_sync();
        eLog("BC 1");
        return;
    }

    eLog("BC 2");
    Responder responder(node->network());
    for (const auto &[id, _] : nodes_by_block) {
        responder.add_identifier(id);
    }

    sync(block.has_value() ? block->getIndex() : BigNumber(0), responder);
    check_status_ = BlockchainSyncStatus::None;
}

std::pair<Transaction, BigNumber> Blockchain::getTxBySender(const ActorId &id, const TokenId &token) {
    return blockIndex.getLastTxBySender(id, token);
}

std::pair<Transaction, BigNumber> Blockchain::getTxByReceiver(const ActorId &id, const TokenId &token) {
    return blockIndex.getLastTxByReceiver(id, token);
}

std::pair<Transaction, BigNumber> Blockchain::getTxBySenderOrReceiver(const ActorId &id, const TokenId &token) {
    return blockIndex.getLastTxBySenderOrReceiver(id, token);
}

std::pair<Transaction, BigNumber> Blockchain::getTxBySenderOrReceiverAndToken(const ActorId &id,
                                                                              const TokenId &token) {
    return blockIndex.getLastTxBySenderOrReceiverAndToken(id, token);
}

std::pair<Transaction, BigNumber> Blockchain::getTxByUser(const ActorId &id, const TokenId &token) {
    return blockIndex.getLastTxBySenderOrReceiver(id, token);
}

BigNumber Blockchain::lastGenesisIdFor(const BigNumber &id) {
    return id / Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS
           * Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
}

std::set<Transaction> Blockchain::getTxsBySenderOrReceiverInRow(const BigNumber &id,
                                                                BigNumber        from,
                                                                int              count,
                                                                ActorId          token) {
    return blockIndex.getTxsBySenderOrReceiverInRow(id, from, count, token);
}

bool Blockchain::sendBlock(const BlockVariant &block) const {
    eFatal("NO sendBlock");
    if (block.isEmpty()) {
        return false;
    }

    // if (block.isGenesisBlock()) {
    //     auto genesisBlock = block.getGenesisBlockConst();
    //     node->network()->send_message(*genesisBlock, MessageType::BlockchainGenesisBlock, SendMode::Neighbours);
    // } else {
    //     auto dataBlock = block.getBlockConst();
    //     node->network()->send_message(*dataBlock, MessageType::BlockchainNewBlock, SendMode::Neighbours);
    // }

    // eLog("Send {}", block);

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

std::expected<BlockVariant, BlockError> Blockchain::createGenesisBlock(const Actor<KeyPrivate> &actor) {
    eLog("Creating genesis block");

    if (blockIndex.getLastSavedId() == -1 || blockIndex.getRecords() == 0) {
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
            auto sender   = transaction.sender();
            auto receiver = transaction.receiver();
            auto tokenId  = transaction.token();
            if (sender == ActorId() || tokenId != TokenId() && transaction.type() == TransactionType::InitContract)
                lastDataRows[{ sender, tokenId }].state -= transaction.amount();
            lastDataRows[{ receiver, tokenId }].state += transaction.amount();
        }
    }

    genesis.addRows(lastDataRows);
    genesis.sign(actor);
    return BlockVariant(genesis);
}

std::expected<BlockVariant, BlockError> Blockchain::createFirstBlock(const Actor<KeyPrivate> &actor) {
    if (blockIndex.getRecords() != 0 || blockIndex.getFirstSavedId() != 0 || blockIndex.getLastSavedId() != 0) {
        return std::unexpected(BlockError::Invalid);
    }

    GenesisBlock genesis;
    genesis.setIndex(BigNumber(0));
    // genesis.addRows(dataRows);
    GenesisDataInfo gdi;
    gdi.state = BigNumberFloat(0);
    gdi.type  = BlockchainConst::DataRowType::Universal;
    genesis.addRow(ActorId(), ActorId(), gdi);
    genesis.addData(actor.id().to_string());
    genesis.sign(actor);
    return BlockVariant(genesis);
}

// Merging //

std::expected<BlockVariant, BlockError> Blockchain::mergeBlockWithLocal(const BlockVariant &received) {
    const auto existed = getBlockByIndex(received.getIndex());

    if (!existed.has_value()) {
        return std::unexpected(BlockError::CantMerge);
    }

    if (existed->getType() == BlockType::Data) {
        // eLog("re {}", received);
        // eLog("ex {}", existed.value());
    }

    if (existed->isEmpty() || received.isEmpty()) {
        return std::unexpected(BlockError::CantMerge);
    }

    if (received.getIndex() == 0) {
        return std::unexpected(BlockError::CantMerge);
    }

    if (!canMergeBlocks(received, existed.value())) {
        // eLog("[Blockchain] Blocks with id {} can't be merged", receivedBlockIndex);
        return std::unexpected(BlockError::CantMerge);
    }

    if (received.isBlock() != existed->isBlock() || received.isGenesisBlock() != existed->isGenesisBlock()) {
        return std::unexpected(BlockError::CantMerge);
    }

    if (received == existed && received.signatures() == existed->signatures()) {
        // eLog("[Blockchain] Blocks {} are equal", received.getIndex());
        return std::unexpected(BlockError::MergeEqual);
    }

    eLog("[Blockchain] Merging block {}", received.getIndex());
    auto merged =
        received.isBlock()
            ? mergeBlocks(received.getBlockConst().value(), existed->getBlockConst().value())
            : mergeGenesisBlocks(received.getGenesisBlockConst().value(), existed->getGenesisBlockConst().value());

    if (!merged.has_value() || (merged.has_value() && merged->isEmpty())) {
        return std::unexpected(BlockError::CantMerge);
    }

    auto mergedVariant = BlockVariant(merged.value());
    // eLog("me {}", mergedVariant);
    this->replaceBlock(mergedVariant);

    return mergedVariant;
}

std::expected<BlockVariant, BlockError> Blockchain::getBlock(SearchEnum::BlockParam type,
                                                             const std::string     &value) {
    switch (type) {
    case SearchEnum::BlockParam::Id:
        return getBlockByIndex(BigNumber(value));
    case SearchEnum::BlockParam::Data:
        return getBlockByData(value);
    case SearchEnum::BlockParam::Hash:
        return getBlockByHash(value);
    default:
        return std::unexpected(BlockError::Unknown);
    }
}

std::pair<Transaction, BigNumber> Blockchain::getTransaction(SearchEnum::TxParam type,
                                                             const std::string  &value,
                                                             const TokenId      &token) {
    switch (type) {
    case SearchEnum::TxParam::UserSenderOrReceiverOrToken:
        return getTxBySenderOrReceiverAndToken(ActorId(value), token);
    case SearchEnum::TxParam::Hash:
        return getTxByHash(value, token);
    case SearchEnum::TxParam::User:
        return getTxByUser(ActorId(value), token);
    case SearchEnum::TxParam::UserReceiver:
        return getTxByReceiver(ActorId(value), token);
    case SearchEnum::TxParam::UserSender:
        return getTxBySender(ActorId(value), token);
    case SearchEnum::TxParam::UserSenderOrReceiver:
        return getTxBySenderOrReceiver(ActorId(value), token);
    default:
        eWarning("Can't get tx: incorrect SearchEnum::TxParam. Value: {}", value);
        return { Transaction(), BigNumber("-1") };
    }
}

bool Blockchain::validateBlock(const BlockVariant &block) {
    return node->actorIndex()->validateBlock(block);
}

BlockVariant Blockchain::validateAndReturnBlock(const BlockVariant &block) const {
    // Get prev block hash and check if it exists in current one :)
    return block;
}

void Blockchain::updateFirstId(const BlockVariant &block) {
    if (!block.isGenesisBlock() || block.getIndex() != 0)
        return;

    if (block.dataService().size() > 1 || block.dataService().empty())
        eFatal("Incorrect first genesis");

    auto firstId = ActorId(*block.dataService().begin());
    if (!firstId.is_zero())
        node->actorIndex()->setFirstId(firstId);
}

std::expected<BlockVariant, BlockError> Blockchain::addBlock(const BlockVariant &block) {
    if (block.isEmpty())
        return std::unexpected(BlockError::Invalid);

    const auto blockId = block.getIndex();
    if (block.isGenesisBlock() && !Blockchain::isGenesisId(blockId)) {
        eLog("[Blockchain] Incorrect genesis");
        // eFatal("Incorrect genesis");
        return std::unexpected(BlockError::Invalid);
    }

    if (block.isBlock() && block.transactions().empty()) {
        return std::unexpected(BlockError::Invalid);
    }

    if (blockId != 0) {
        auto prevBlock     = this->getBlockByIndex(blockId - 1);
        auto nextBlock     = getBlockByIndex(blockId + 1);
        auto lastRealBlock = this->getLastRealBlock();
        auto lastGenesis   = blockIndex.getLastGenesisBlock(blockId - 1);

        auto now_block = this->getBlockByIndex(blockId);
        if (now_block.has_value() && now_block->getHash() == block.getHash()) {
            return std::unexpected(BlockError::BlockEqual);
        }

        if (nextBlock.has_value()) {
            // eLog("[Blockchain] Already chained");
            return std::unexpected(BlockError::AlreadyChained);
        }

        if (!block.isGenesisBlock() && !prevBlock.has_value()) {
            // sync();
            start_sync();
            return std::unexpected(BlockError::Invalid);
        }

        if (!block.isGenesisBlock() && blockId > prevBlock->getIndex() + 1) {
            eLog("[Blockchain] New block id is greater than last id, sync request");
            start_sync();
            return std::unexpected(BlockError::Invalid);
        }

        if (!lastGenesis.has_value()) {
            return std::unexpected(BlockError::Invalid);
        }

        auto checkedPrevHash  = block.isGenesisBlock() ? block.getPrevGenHash() : block.getPrevHash();
        auto expectedPrevHash = block.isGenesisBlock() ? lastGenesis->getHash() : prevBlock->getHash();
        if (checkedPrevHash != expectedPrevHash) {
            // jestko
            eLog("[Blockchain] Can't chained, sync request");
            // eLog("jb {}", block);
            // if (lastBlock.has_value())
            //     eLog("lb {}", lastBlock.value());
            // if (lastGenesis.has_value())
            //     eLog("lg {}", lastGenesis.value());
            // if (block.getType() != BlockType::Dummy)
            if (lastRealBlock.has_value())
                removeBlock(lastRealBlock.value());

            // TODO: hashs incoming, not sync, remove block
            // TODO: package for removing last block?
            start_sync();
            // sync(blockId - 1); // TODO: request only chel who sended block?
            return std::unexpected(BlockError::Invalid);
        }
    }

    if (block.getType() == BlockType::Genesis) {
        // eLog("[Blockchain] Adding a genesis block {} to storage", block.getIndex());
    } else {
        // eLog("[Blockchain] Adding a block {} to storage {}", block.getIndex(), block.getType());
    }

    this->updateFirstId(block);

    if (blockId < 0) {
        return std::unexpected(BlockError::Invalid);
    }

    // const auto           &transactions = block.transactions();
    // std::set<Transaction> transactions_approved;
    // // TODO: if remove tx -> ignore in prove
    // for (const auto &tx : block.transactions()) {
    //     auto res = proveTransaction(tx, transactions);

    //     if (res == TransactionProveError::NoError || res == TransactionProveError::SelfPleasure) {
    //         transactions_approved.insert(tx);
    //     }
    // }

    // recalc hash

    BlockVariant newBlock(block);
    // newBlock.set_transactions(transactions_approved);

    if (block.getType() != BlockType::Dummy && block.getIndex() != BigNumber(0)) {
        signBlock(newBlock);
    }

    TIMER_START(BlockIndexAdd)
    const auto res = blockIndex.addBlock(newBlock);
    TIMER_END(BlockIndexAdd)

    const auto blockType = newBlock.getType();

    if (!res.has_value()) {
        if (res.error() == BlockError::AlreadyExists && newBlock.getIndex() > 0) {
            if (newBlock.getType() == BlockType::Dummy) {
                return res;
            }

            auto mergeRes = mergeBlockWithLocal(newBlock);
            // TODO: prove also
            return mergeRes;
        }

        return res;
    }

    if (blockType == BlockType::Data) {
        emit updateLastTransactionList();
    }

    eLog("[Blockchain] Block {} is added, {}", blockId, blockType);

    emit blockAdded(newBlock);

    if (blockId > 0 && blockId % Dfs::Reward::coinProductionAlgorithmTick == 0) {
        // node->dataMiningManager()->requestCoinReward();
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
    if (receivedBlock.signatures().empty() || existedBlock.signatures().empty()
        || receivedBlock.getType() != existedBlock.getType()
        || receivedBlock.getIndex() != existedBlock.getIndex()) {
        return false;
    }

    return true;
}

std::expected<BlockVariant, BlockError> Blockchain::mergeBlocks(const Block &blockA, const Block &blockB) {
    // eLog("[Blockchain] Attempting to merge {} and {}", blockA, blockB);

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

std::expected<BlockVariant, BlockError> Blockchain::mergeGenesisBlocks(const GenesisBlock &blockA,
                                                                       const GenesisBlock &blockB) {
    eLog("[Blockchain] Attempting to merge {} and {}", blockA, blockB);

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
    block.sign(node->accountController()->mainActor());
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

            if (tx.sender() == userId && tx.token() == tokenId && tx.type() == TransactionType::Reward) {
                balance += tx.amount();
                continue;
            }

            if (tx.sender() == userId && tx.token() == tokenId && tx.type() == TransactionType::InitContract) {
                balance += tx.amount();
                continue;
            }

            if (tx.receiver() == userId && tx.token() == tokenId) {
                balance += tx.amount();
            }

            if (tx.sender() == userId && tx.token() == tokenId) {
                balance -= tx.amount();
            }
        }
    }

    return balance;
}

void Blockchain::showBlockchain() const {
    eLog("[Blockchain] Show blockchain:");

    auto genBlock = blockIndex.getLastGenesisBlock();
    // eLog("Last genesis: {}", genBlock);

    int i = 0;

    // do {
    //     auto currentBlock = blockIndex.getBlockById(i);

    //     if (currentBlock->isGenesisBlock())
    //         eLog("{}", currentBlock->getGenesisBlockConst());
    //     else
    //         eLog("{}", currentBlock->getBlock());

    //     i++;
    // } while (!currentBlock.isEmpty());
}

BigNumber Blockchain::getBlockCount() {
    // eLog("[Blockchain] Count: {}", this->blockIndex.getLastSavedId());
    return this->blockIndex.getLastSavedId();
}

void Blockchain::addBlockNetwork(const BlockVariant         &block,
                                 const Responder            &responder,
                                 const NetworkPackageStorage package,
                                 bool                        resend) {
    if (status_ == BlockchainStatus::Sync && block.getIndex() != BigNumber(0)) {
        //
        return;
    }

    if (block.getIndex() > 0 && block.isGenesisBlock()) {
        // eLog("!!!!!!!!!!!");
    }
    if (block.getType() == BlockType::Data) {
        // eLog("???????");
    }

    auto lastBlock = this->getLastBlock();
    if (block.getIndex() != 0 && (!lastBlock.has_value() || (lastBlock.has_value() && block.isEmpty()))) {
        return;
    }

    if (block.getType() != BlockType::Dummy) {
        this->removeDummyBlocks();
    }

    auto res = addBlock(block);

    if (!res.has_value()) {
        switch (res.error()) {
        case BlockError::AlreadyChained: {
            if (blockIndex.lastSavedId - 100 <= block.getIndex() && !responder.identifiers().empty()) {
                syncResponse(block.getIndex(), responder);
            } // else {
            //     node->network()->send_message("",
            //                                   MessageType::BlockchainAnarchy,
            //                                   MessageStatus::Response,
            //                                   messageId,
            //                                   SendMode::Focused);
            // }

            return;
        }
        default:
            break;
        }

        return;
    }

    // if (res->getType() != BlockType::Dummy) {
    // sendBlock(res.value());

    if (resend) {
        if (block == res.value()) {
            node->network()->sendBrodcastMessageFurther(package);
        } else {
            node->network()->send_message(res.value(), MessageType::BlockchainNewBlock, SendMode::Broadcast);
        }
    }
    // broadcast another block
    // }
    // sendBlockByNumber(block.getIndex());

    // notifications for clients
    if (res->getType() != BlockType::Data) {
        return;
    }

    auto       transactions = block.transactions();
    const auto accounts     = node->accountController()->accountsIds();
    for (const auto &transaction : transactions) {
        // vefify

        if (transaction.type() == TransactionType::InitContract) {
            node->actorIndex()->getActor(transaction.sender());
            // TODO: subscribe dfs for waiting token json?
        }

        for (const auto &accountId : accounts) {
            if (transaction.sender() == accountId || transaction.receiver() == accountId) {
                emit updateSelf(block.getIndex());
            }
        }
    }

    // if (list.contains(tmp.getSender())) {
    //     emit newNotify({ QDateTime::currentMSecsSinceEpoch(),
    //                      Notification::NotifyType::TxToUser,
    //                      tmp.getReceiver().toByteArray() });
    // } else if (list.contains(tmp.getReceiver())) {
    //     emit newNotify({ QDateTime::currentMSecsSinceEpoch(),
    //                      Notification::NotifyType::TxToMe,
    //                      tmp.getSender().toByteArray() });
    // }
}

// Actors //
TransactionProveError Blockchain::proveTransaction(const Transaction          &tx,
                                                   const std::set<Transaction> transactions) {
    // eLog("[Blockchain] Transaction prove started: {}", tx);
    // TODO: temp, remove
    if (tx.amount() == 0) {
        return TransactionProveError::AmountZero;
    }

    if (tx.amount() < 0) {
        return TransactionProveError::AmountLessZero;
    }

    ActorId        targetSender   = tx.sender();
    ActorId        targetReceiver = tx.receiver();
    const ActorId &mainActorId    = node->accountController()->mainActor().id();

    const auto accounts = node->accountController()->accountsIds();
    for (const auto &accountId : accounts) {
        if (targetSender == accountId || targetReceiver == accountId) {
            return TransactionProveError::SelfPleasure;
        }
    }

    auto tx_copy = tx;
    tx_copy.calculate_hash();
    if (tx.hash() != tx_copy.hash()) {
        return TransactionProveError::WrongHash;
    }

    auto res = this->blockIndex.getLastTxByHash(tx_copy.hash(), tx.token());
    if (res.second != BigNumber(-1)) {
        return TransactionProveError::Duplicate;
    }

    if (targetSender.is_zero()) {
        return TransactionProveError::SenderZero;
    }

    Actor<KeyPublic> senderActor;
    senderActor = node->actorIndex()->getActor(targetSender);
    if (senderActor.empty()) {
        return TransactionProveError::SenderNotExists;
    }

    if (tx.type() == TransactionType::Burn) {
        if (!tx.receiver().is_zero()) {
            return TransactionProveError::BurnIncorrectReceiver;
        }

        bool verify = tx.verify(senderActor);
        if (!verify) {
            return TransactionProveError::InvalidSignature;
        }

        return TransactionProveError::NoError;
    }

    if (targetReceiver.is_zero()) {
        return TransactionProveError::ReceiverZero;
    }

    Actor<KeyPublic> receiverActor;
    receiverActor = node->actorIndex()->getActor(targetReceiver);
    if (receiverActor.empty()) {
        return TransactionProveError::ReceiverNotExists;
    }

    if (tx.type() != TransactionType::Reward && tx.type() != TransactionType::InitContract
        && targetSender == targetReceiver) {
        return TransactionProveError::IdenticalSenderReceiver;
    }

    auto block = getLastRealBlock();
    if (!block.has_value()) {
        return TransactionProveError::EmptyBlockchain;
    }
    if (block->isEmpty()) {
        return TransactionProveError::EmptyBlockchain;
    }

    if (tx.signature().empty()) {
        return TransactionProveError::MissingSignature;
    }

    bool verify = tx.verify(senderActor);
    if (!verify) {
        return TransactionProveError::InvalidSignature;
    }

    if (tx.type() == TransactionType::Reward) {
        return TransactionProveError::NoError;
    }

    // special conditions: receiver is null - coins burning, contract creation
    // TODO: InitContract: check duplicate
    if (tx.type() == TransactionType::InitContract) {
        auto count = tx.amount();
        if (count < 0 || count >= Token::MAX_TOKEN_COUNT) {
            return TransactionProveError::InvalidTokenCount;
        }

        return TransactionProveError::NoError;
    }

    TokenId        token                = tx.token();
    BigNumberFloat senderCurrentBalance = getUserBalance(targetSender, token);
    BigNumberFloat amount_res;

    for (const Transaction &tx : std::as_const(transactions)) {
        if (tx.sender() == targetSender && tx.token() == token) {
            amount_res -= tx.amount();
        } else if (tx.receiver() == targetReceiver && tx.token() == token) {
            amount_res += tx.amount();
        }
    }

    senderCurrentBalance += amount_res;
    BigNumberFloat transactionAmount = tx.amount();
    BigNumberFloat transactionFee; // transactionAmount / 100;
    BigNumberFloat senderNewBalance = senderCurrentBalance - transactionAmount - transactionFee;

    if (senderNewBalance < 0 && targetSender != ActorId() /* && mainActorId == firstId */) {
        return TransactionProveError::SenderBalanceBelowZero;
    }

    return TransactionProveError::NoError;
}

void Blockchain::process() {
    connect(this, &Blockchain::addBlockFromNetwork, this, &Blockchain::addBlockNetwork);
    connect(this, &Blockchain::syncResponseFromNetwork, this, &Blockchain::syncResponse);
    connect(this, &Blockchain::syncResponseVectorFromNetwork, this, &Blockchain::syncResponseVector);

    timer_sync = new QTimer(this);
    connect(timer_sync, &QTimer::timeout, this, &Blockchain::timer_sync_tick);
}

// Other //

BlockIndex &Blockchain::getBlockIndex() {
    return blockIndex;
}

void Blockchain::removeAll() {
    this->blockIndex.removeAll();
    QFile(QString::fromStdString(BlockchainConst::TMP_GENESIS_BLOCK)).remove();
}

void Blockchain::timer_sync_tick() {
    eLog("[Blockchain] Timer sync tick");
    timer_sync->stop();
    status_ = BlockchainStatus::Timered;
    start_sync();
}
