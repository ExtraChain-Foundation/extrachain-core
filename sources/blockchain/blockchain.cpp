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
#include <QtConcurrent/qtconcurrentrun.h>

#include "blockchain/blockchain.h"
// #include "dfs/dfs_controller.h"
#include "blockchain/actor_index.h"
// #include "managers/data_mining_manager.h"
#include "managers/transaction_manager.h"
#include "network/network_manager.h"

Blockchain::Blockchain(ExtraChainNode *node)
    : /*QObject(node)
    , */
    node(node)
    , transaction_cache_(node, this) {

    // get first id on start
    auto genesis = blockIndex.getGenesisBlockById(BigNumber(0));
    if (genesis.has_value()) {
        update_network_id(genesis.value());
    }

#ifdef IS_RC
    // TODO: move as set
    if (blockIndex.first_saved_id != BigNumber(0)) {
        mode = BlockchainMode::Light;
    }
#endif
}

Blockchain::~Blockchain() {
}

std::expected<BlockVariant, BlockError> Blockchain::read_block_by_id(const BigNumber &id,
                                                                     const bool       makeRequestBlock) {
    auto block = blockIndex.read_block_by_id(id);
    if (!block.has_value())
        return std::unexpected(BlockError::NotExists);
    // if (block->isEmpty() && index >= 0 && makeRequestBlock) {
    //     std::pair<BlockType, BigNumber> requestData(BlockType::Data, index);
    //     // node->network()->send_message(requestData, MessageType::BlockchainRequestBlock);
    //     return std::unexpected(BlockError::NotExists);
    // }
    return block;
}

// Blocks //

std::expected<BlockVariant, BlockError> Blockchain::read_last_block() const {
    auto block = blockIndex.getLastBlock();
    return block;
    // return validateAndReturnBlock(block);
}

BigNumber Blockchain::getBlocksStored() const {
    return blockIndex.getLastSavedId() - blockIndex.getFirstSavedId() + 1;
}

std::expected<BlockVariant, BlockError> Blockchain::getBlockByData(const std::string &data) {
    auto block = blockIndex.getBlockByData(data);
    return block;
    // return validateAndReturnBlock(block);
}

std::expected<BlockVariant, BlockError> Blockchain::search_block_by_hash(const std::string &hash) {
    auto block = blockIndex.search_block_by_hash(hash);
    return block;
    // return validateAndReturnBlock(block);
}

std::pair<Transaction, BigNumber> Blockchain::search_tx_by_hash(const std::string &hash, const ActorId &token) {
    return blockIndex.getLastTxByHash(hash, token);
}

void Blockchain::sync(const BigNumber &from, std::optional<Responder> responder) {
    auto lastBlock = read_last_block();
    auto fromBlock = lastBlock.has_value() ? lastBlock->id() : from;
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
        eLog("[Blockchain] Sync from {}", fromBlock);
        node->network()->send_message(fromBlock,
                                      MessageType::BlockchainSync,
                                      SendMode::Focused,
                                      MessageStatus::Request,
                                      responder.value());
    }
}

void Blockchain::syncResponse(const BigNumber &fromBlock, const Responder &responder) {
    auto lastBlock = read_last_block();
    if (!lastBlock.has_value()) {
        return;
    }

    BigNumber lastIndex = lastBlock->id();

    if (lastIndex < fromBlock) {
        // this->sync();
        // eLog("{} {}", lastIndex, fromBlock);
        return;
    }

    BigNumber from = fromBlock;
    if (from < 0)
        from = 0;

    // std::vector<BlockVariant> blocks;
    std::vector<std::pair<BigNumber, std::string>> blocks;
    blocks.reserve(1000);

    if (fromBlock != 0) {
        auto zero_block = blockIndex.read_block_by_id(BigNumber(0));
        if (!zero_block.has_value()) {
            return;
        }
        auto  block_path = blockIndex.buildFilePath(zero_block->id());
        QFile file(block_path.c_str());
        auto  is_open = file.open(QFile::ReadOnly);
        if (!is_open) {
            return;
        }
        auto content = file.readAll().toStdString();
        blocks.push_back({ zero_block->id(), content });
    }

    for (; from <= lastIndex; from++) {
        // eLog("[Blockchain] Send sync {}", from);
        auto block = blockIndex.read_block_by_id(from);

        if (!block.has_value()) {
            continue;
        }

        auto  block_path = blockIndex.buildFilePath(block->id());
        QFile file(block_path.c_str());
        auto  is_open = file.open(QFile::ReadOnly);
        if (!is_open) {
            return;
        }
        auto content = file.readAll().toStdString();
        blocks.push_back({ block->id(), content });
        // blocks.push_back(block.value());

        if (blocks.size() >= 1000) {
            auto ser = MessagePack::serialize(blocks);
            auto res = qCompress(QByteArray::fromStdString(ser));

            responder.send_response(res.toStdString(),
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

    // mode == BlockchainMode::Light ???????????????

    if (blocks.empty()) {
        return;
    }

    auto ser = MessagePack::serialize(blocks);
    auto res = qCompress(QByteArray::fromStdString(ser));

    responder.send_response(res.toStdString(),
                            MessageType::BlockchainSyncBlocks,
                            SendMode::Focused,
                            MessageStatus::Response);

    // eLog("[Blockchain] Send for sync: from {} to {}", fromBlock, lastIndex);
}

void Blockchain::syncResponseVector(const std::string           &blocks_,
                                    const Responder             &responder,
                                    const NetworkPackageStorage &package_storage) {
    auto des           = qUncompress(QByteArray::fromStdString(blocks_)).toStdString();
    auto blocks_result = MessagePack::deserialize<std::vector<std::pair<BigNumber, std::string>>>(des);
    if (!blocks_result.has_value()) {
        return;
    }
    auto blocks = blocks_result.value();
    if (blocks.empty()) {
        eLog("[Blockchain] Sync: incoming empty blocks... Why?");
    }

    auto lastBlock = read_last_block();
    if (lastBlock.has_value()) {
        BigNumber lastIndex = lastBlock->id();

        if (blocks.size() > 2) {
            eLog("-> _____ {} {}",
                 lastIndex.to_string(NumeralBase::Dec),
                 blocks[1].first.to_string(NumeralBase::Dec));
            if (lastIndex > blocks[1].first) {
                return;
            }
        }
    }

    eLog("[Blockchain] Sync: incomining {} blocks... First: {}, last: {}",
         blocks.size(),
         blocks.front().first,
         blocks.back().first);

    for (const auto &block : blocks) {
        auto  block_path = blockIndex.buildFilePath(block.first);
        QFile file(block_path.c_str());
        file.open(QFile::WriteOnly);
        file.write(QByteArray::fromStdString(block.second));
        file.close();

        if (mode == BlockchainMode::Light && block.first != BigNumber(0)) {
            blockIndex.update_last_id(block.first);
        }
        if (mode == BlockchainMode::Full) {
            blockIndex.update_last_id(block.first);
        }
    }

    // TODO: check all blocks

#ifdef IS_RC
    std::thread rc_thread([this, blocks]() {
        std::set<BigNumber> self_block_ids;

        for (const auto &block : blocks) {
            auto added_block = blockIndex.read_block_by_id(block.first);
            if (!added_block.has_value()) {
                continue;
            }

            if (added_block->id() == BigNumber(0)) {
                emit zeroBlock();
            }

            auto       transactions = added_block->transactions();
            const auto accounts     = node->accountController()->accountsIds();

            for (const auto &transaction : transactions) {
                if (transaction.type() == TransactionType::InitContract) {
                    node->actorIndex()->getActor(transaction.sender());
                }

                for (const auto &accountId : accounts) {
                    if (transaction.sender() == accountId || transaction.receiver() == accountId) {
                        self_block_ids.insert(added_block->id());
                    }
                }
            }
        }

        for (const auto &index : self_block_ids) {
            emit updateSelf(index);
        }

        // if (blockIndex.last_saved_id >= sync_last_index - 1) {
        //     emit need_check();
        // }
    });

    rc_thread.detach();
#endif

    emit syncProgress(blockIndex.last_saved_id);
    eLog("-> {} {}", blockIndex.last_saved_id, sync_last_index - 1);
    if (blockIndex.last_saved_id >= sync_last_index - 1) {
        eLog("-> START CHECK");
        status_ = BlockchainStatus::Maybe;
        emit statusChanged(status_);
        start_check();
    }
}

BlockchainStatus Blockchain::status() {
    return status_;
}

void Blockchain::start_sync() {
    // start timer, after end -> again request
    if (status_ == BlockchainStatus::Sync) {
        // eLog("BC 11 start_sync return");
        return;
    }

    timer_sync->stop();
    timer_sync->start(10000);

    if (status_ != BlockchainStatus::Sync) {
        status_ = BlockchainStatus::Sync;
        emit statusChanged(status_);
    }

    last_info_.clear();
    set_sync_status(BlockchainSyncStatus::LastInfo);
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
        // eLog("BC 12 start_check return");
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
    auto        block      = this->read_last_block();
    BigNumber   block_id   = block.has_value() ? block->id() : BigNumber(-1);
    std::string hash       = block.has_value() ? block->getHash() : "";
    auto        zero_block = this->read_block_by_id(BigNumber(0));
    auto        last_info  = BlockchainLastInfo { .last_block_id = block_id,
                                                  .last_hash     = hash,
                                                  .zero_date = zero_block.has_value() ? zero_block->getDate() : 0 };
    // eLog("network_status_sync_request, send: {}", last_info);
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

    auto zero_block = read_block_by_id(BigNumber(0));
    if (zero_block.has_value() && last_info.last_hash != "" && last_info.last_block_id != BigNumber(-1)
        && zero_block->getDate() < last_info.zero_date) {
        removeAll(false, true);
    }

    int count = std::min(requests_count, 5);

    last_info_.insert({ *responder.identifiers().begin(), last_info });

    if (sync_status_ == BlockchainSyncStatus::LastInfo && last_info_.size() >= count) {
        set_sync_status(BlockchainSyncStatus::Blocks);
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
    auto block = this->read_last_block();

    if (last_info_.empty()) {
        eLog("BC 5");
        return;
    }

    bool need_sync = false;

    if (!block.has_value()) {
        for (const auto &[_, info] : last_info_) {
            if (info.last_block_id >= 0 && !info.last_hash.empty()) {
                need_sync = true;
                break;
            }
        }
    } else {
        const auto my_index = block->id();
        const auto my_hash  = block->getHash();

        for (const auto &[_, info] : last_info_) {
            if (info.last_block_id > my_index) {
                need_sync = true;
                // remove_last_block();
                // blockIndex.removeById(my_index);
                eLog("[Blockchain] Sync: remove block {}", my_index);
                break;
            }
            if (info.last_block_id == my_index && info.last_hash != my_hash) {
                need_sync = true;
                // remove_last_block();
                // blockIndex.removeById(my_index);
                eLog("[Blockchain] Sync: remove block {}", my_index);
                break;
            }
        }
    }

    if (!need_sync) {
        set_sync_status(BlockchainSyncStatus::None);
        check_status_ = BlockchainSyncStatus::None;
        status_       = BlockchainStatus::Ready;
        emit statusChanged(status_);
        timer_sync->stop();

        emit syncEnd();

        eLog("BC 4");
        return; // end sync
    }

    int connections = requests_count;
    int max_nodes   = std::min(connections, 5);

    std::vector<std::pair<std::string, BigNumber>> nodes_by_block;
    for (const auto &[id, info] : last_info_) {
        if (info.last_block_id >= 0 && !info.last_hash.empty()) {
            nodes_by_block.emplace_back(id, info.last_block_id);
        }
    }

    // TODO: recheck
    if (nodes_by_block.empty()) {
        eLog("BC 3");
        set_sync_status(BlockchainSyncStatus::None);
        check_status_ = BlockchainSyncStatus::None;
        status_       = BlockchainStatus::Ready;
        emit statusChanged(status_);
        timer_sync->stop();

        emit syncEnd();

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
        // TODO
        responder.add_identifier(id);
    }

    auto last_block = this->read_last_block();
    auto sync_index = mode == BlockchainMode::Light
                          ? calculate_genesis_id_for_block(nodes_by_block.front().second)
                          : (last_block.has_value() ? last_block->id() + 1 : BigNumber(0));
    sync_last_index = nodes_by_block.front().second;

    if (blockIndex.last_saved_id > sync_last_index) {
        eLog("Not need sync");
        start_check();
        return;
    }

    eLog("{}", sync_last_index);
    sync(sync_index, responder);
    check_status_ = BlockchainSyncStatus::None;
    emit syncStart(sync_index, sync_last_index);
    eLog("syncStart");
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

BigNumber Blockchain::calculate_genesis_id_for_block(const BigNumber &id) {
    return id / Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS
           * Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
}

std::unordered_map<ActorId, std::vector<Transaction>> Blockchain::getTxsBySenderOrReceiverInRow(
    const std::vector<ActorId> &id,
    BigNumber                   from,
    int                         count,
    ActorId                     token) {
    return blockIndex.getTxsBySenderOrReceiverInRow(id, from, count, token);
}
void Blockchain::getTxsBySenderOrReceiverInRowInThread(const std::vector<ActorId> &id,
                                                       BigNumber                   from,
                                                       int                         count,
                                                       ActorId                     token) {
    auto result = QtConcurrent::run([=, this] {
        return blockIndex.getTxsBySenderOrReceiverInRow(id, from, count, token);
    });

    auto *watcher = new QFutureWatcher<std::unordered_map<ActorId, std::vector<Transaction>>>(this);

    connect(watcher,
            &QFutureWatcher<std::unordered_map<ActorId, std::vector<Transaction>>>::finished,
            this,
            [=, this]() {
                auto map = watcher->result();
                eLog("Async task completed with result: {}", map.size());
                emit this->resultTransactions(result.result());
                watcher->deleteLater();
                emit testSignal();
            });

    watcher->setFuture(result);
}

bool Blockchain::sendBlock(const BlockVariant &block) const {
    eFatal("NO sendBlock");
    if (block.isEmpty()) {
        return false;
    }

    // if (block.isGenesisBlock()) {
    //     auto genesisBlock = block.getGenesisBlockConst();
    //     node->network()->send_message(*genesisBlock,
    //     MessageType::BlockchainGenesisBlock,
    //     SendMode::Neighbours);
    // } else {
    //     auto dataBlock = block.getBlockConst();
    //     node->network()->send_message(*dataBlock,
    //     MessageType::BlockchainNewBlock,
    //     SendMode::Neighbours);
    // }

    // eLog("Send {}", block);

    return true;
}

void Blockchain::sendBlockByNumber(const BigNumber &index) const {
    auto block = blockIndex.read_block_by_id(index);

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

bool Blockchain::is_genesis_id(const BigNumber &id) {
    return id == 0 || id % Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS == 0;
}

std::expected<BlockVariant, BlockError> Blockchain::create_genesis_block(const Actor<KeyPrivate> &actor) {
    eLog("Creating genesis block");

    if (blockIndex.getLastSavedId() == -1 || blockIndex.getFirstSavedId() == -1) {
        return std::unexpected(BlockError::EmptyBlockchain);
    }

    GenesisBlock genesis;
    auto         lastBlock        = read_last_block();
    auto         lastRealBlock    = read_last_block();
    auto         lastGenesisBlock = blockIndex.getLastGenesisBlock();

    if (!lastGenesisBlock.has_value())
        return std::unexpected(BlockError::NoGenesis);
    if (!lastRealBlock.has_value())
        return std::unexpected(BlockError::Unknown);

    genesis.setPrev(lastRealBlock.value());
    genesis.setPrevGen(lastGenesisBlock.value());

    auto lastDataRows = lastGenesisBlock->dataRows();

    for (auto i = lastGenesisBlock->id();
         i != lastGenesisBlock->id() + Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
         i++) {
        auto block = read_block_by_id(i);

        if (!block.has_value()) {
            continue;
        }

        if (block->isEmpty() || !block->isBlock() || block->getType() != BlockType::Data) {
            continue;
        }

        // tx check
        auto transactions = block->transactions();
        for (auto &tx : transactions) {
            auto sender   = tx.sender();
            auto receiver = tx.receiver();
            auto tokenId  = tx.token();

            if (tx.type() == TransactionType::Reward && tx.sender() == tx.receiver()) {
                lastDataRows[{ sender, tokenId }].state += tx.amount();
                continue;
            }

            if (tx.type() == TransactionType::InitContract && tx.sender() == tx.receiver()) {
                lastDataRows[{ sender, tokenId }].state += tx.amount();
                continue;
            }

            if (tx.type() == TransactionType::Conversion && tx.sender() == tx.receiver()) {
                auto from_token = ActorId::create(tx.data());
                if (!from_token.has_value()) {
                    continue;
                }

                if (from_token.value() == tx.token()) {
                    continue;
                }

                lastDataRows[{ sender, from_token.value() }].state -= tx.amount();
                lastDataRows[{ sender, tokenId }].state += tx.amount();
                continue;
            }

            lastDataRows[{ sender, tokenId }].state -= tx.amount();
            lastDataRows[{ receiver, tokenId }].state += tx.amount();
        }
    }

    genesis.addRows(lastDataRows);
    genesis.sign(actor);
    return BlockVariant(genesis);
}

std::expected<BlockVariant, BlockError> Blockchain::create_mega_genesis_block(const Actor<KeyPrivate> &actor) {
    eLog("Creating MEGA genesis block");

    if (blockIndex.getLastSavedId() == -1 || blockIndex.getFirstSavedId() == -1) {
        return std::unexpected(BlockError::EmptyBlockchain);
    }

    GenesisBlock genesis;
    genesis.set_id(BigNumber(0));

    auto zero_genesis = blockIndex.read_block_by_id(BigNumber(0));
    auto lastBlock    = read_last_block();

    if (!lastBlock.has_value() || !zero_genesis.has_value()) {
        return std::unexpected(BlockError::NoLastBlock);
    }

    int rewards     = 0;
    int conversions = 0;

    // get from zero block
    GenesisDataRows map = zero_genesis->dataRows();
    eLog("[Mega] Zero size: {}", map.size());

    // tx check
    for (auto i = BigNumber(0); i <= lastBlock->id(); i++) {
        auto block = read_block_by_id(i);

        if (!block.has_value()) {
            eCritical("[MEGA] Block {} not exists", i);
            continue;
        }

        if (!block->isBlock() || block->getType() != BlockType::Data) {
            continue;
        }

        if (block->isEmpty()) {
            eCritical("[MEGA] Block {} is empty", block->id());
            continue;
        }

        // tx check
        auto transactions = block->transactions();
        eInfo("[MEGA] Block {} from {} ({} transactions)",
              block->id().to_string(NumeralBase::Dec),
              lastBlock->id().to_string(NumeralBase::Dec),
              transactions.size());

        for (auto &tx : transactions) {
            if (tx.type() == TransactionType::Reward) {
                map[{ tx.sender(), tx.token() }].state += tx.amount();
                rewards += 1;
                continue;
            }

            if (tx.type() == TransactionType::InitContract) {
                map[{ tx.sender(), tx.token() }].state += tx.amount();
                continue;
            }

            if (tx.type() == TransactionType::Conversion && tx.sender() == tx.receiver()) {
                conversions += 1;
                auto from_token = ActorId::create(tx.data());
                if (!from_token.has_value()) {
                    continue;
                }

                if (from_token.value() == tx.token()) {
                    continue;
                }

                map[{ tx.sender(), from_token.value() }].state -= tx.amount();
                map[{ tx.sender(), tx.token() }].state += tx.amount();

                continue;
            }

            map[{ tx.sender(), tx.token() }].state -= tx.amount();
            map[{ tx.receiver(), tx.token() }].state += tx.amount();
        }
    }

    for (const auto &[actor_token, state] : map) {
        if (state.state < 0) {
            map[actor_token].state = BigNumberFloat(0);
        }
    }

    eLog("[MEGA] Rewards count: {}, conversions count: {}", rewards, conversions);

    genesis.addData(actor.id().to_string());
    genesis.addRows(map);
    genesis.sign(actor);
    return BlockVariant(genesis);
}

std::expected<BlockVariant, BlockError> Blockchain::create_zero_genesis_block(const Actor<KeyPrivate> &actor) {
    if (blockIndex.getFirstSavedId() != -1 || blockIndex.getLastSavedId() != -1) {
        return std::unexpected(BlockError::CantCreateFirst);
    }

    GenesisBlock genesis;
    genesis.set_id(BigNumber(0));
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
    const auto existed = read_block_by_id(received.id());

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

    if (received.id() == 0) {
        return std::unexpected(BlockError::CantMerge);
    }

    if (!canMergeBlocks(received, existed.value())) {
        // eLog("[Blockchain] Blocks with id {} can't be
        // merged", receivedBlockIndex);
        return std::unexpected(BlockError::CantMerge);
    }

    if (received.isBlock() != existed->isBlock() || received.is_genesis() != existed->is_genesis()) {
        return std::unexpected(BlockError::CantMerge);
    }

    if (received == existed && received.signatures() == existed->signatures()) {
        // eLog("[Blockchain] Blocks {} are equal",
        // received.id());
        return std::unexpected(BlockError::MergeEqual);
    }

    eLog("[Blockchain] Merging block {}", received.id());
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
        return read_block_by_id(BigNumber(value));
    case SearchEnum::BlockParam::Data:
        return getBlockByData(value);
    case SearchEnum::BlockParam::Hash:
        return search_block_by_hash(value);
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
        return search_tx_by_hash(value, token);
    case SearchEnum::TxParam::User:
        return getTxByUser(ActorId(value), token);
    case SearchEnum::TxParam::UserReceiver:
        return getTxByReceiver(ActorId(value), token);
    case SearchEnum::TxParam::UserSender:
        return getTxBySender(ActorId(value), token);
    case SearchEnum::TxParam::UserSenderOrReceiver:
        return getTxBySenderOrReceiver(ActorId(value), token);
    default:
        eWarning(
            "Can't get tx: incorrect SearchEnum::TxParam. "
            "Value: {}",
            value);
        return { Transaction(), BigNumber("-1") };
    }
}

bool Blockchain::validateBlock(const BlockVariant &block) {
    return node->actorIndex()->validateBlock(block);
}

BlockVariant Blockchain::validateAndReturnBlock(const BlockVariant &block) const {
    // Get prev block hash and check if it exists in current
    // one :)
    return block;
}

void Blockchain::update_network_id(const BlockVariant &block) {
    if (!block.is_genesis() || block.id() != 0)
        return;

    if (block.dataService().empty())
        eFatal("Incorrect first genesis");

    auto firstId = ActorId(*block.dataService().begin());
    if (!firstId.is_zero())
        node->actorIndex()->set_network_id(firstId);
}

std::expected<BlockVariant, BlockError> Blockchain::addBlock(const BlockVariant &block) {
    if (block.isEmpty()) {
        eLog("[Blockchain] Try to add: block is empty");
        return std::unexpected(BlockError::Empty);
    }

    const auto blockId = block.id();
    if (blockId < 0) {
        return std::unexpected(BlockError::IncorrectBlockId);
    }

    if (block.is_genesis() && !Blockchain::is_genesis_id(blockId)) {
        eLog("[Blockchain] Try to add: incorrect genesis");
        // eFatal("Incorrect genesis");
        return std::unexpected(BlockError::IncorrectGenesisId);
    }

    if (block.isBlock() && block.transactions().empty()) {
        eLog("[Blockchain] Try to add: to transactions");
        return std::unexpected(BlockError::NoTransactions);
    }

    if (blockId != 0) {
        auto prevBlock     = this->read_block_by_id(blockId - 1);
        auto nextBlock     = read_block_by_id(blockId + 1);
        auto lastRealBlock = this->read_last_block();
        auto lastGenesis   = blockIndex.getLastGenesisBlock(blockId - 1);

        auto now_block = this->read_block_by_id(blockId);
        if (now_block.has_value() && now_block->getHash() == block.getHash()) {
            return std::unexpected(BlockError::BlockEqual);
        }

        if (nextBlock.has_value()) {
            // eLog("[Blockchain] Already chained");
            return std::unexpected(BlockError::AlreadyChained);
        }

        if (!block.is_genesis() && !prevBlock.has_value()) {
            // sync();
            remove_last_block();
            start_sync();
            return std::unexpected(BlockError::NoPrevBlock);
        }

        if (!block.is_genesis() && blockId > prevBlock->id() + 1) {
            eLog(
                "[Blockchain] New block id is greater than "
                "last id, sync request");
            remove_last_block();
            start_sync();
            return std::unexpected(BlockError::GreaterLast);
        }

        if (!lastGenesis.has_value()) {
            return std::unexpected(BlockError::NoLastGenesis);
        }

        auto checkedPrevHash  = block.is_genesis() ? block.getPrevGenHash() : block.getPrevHash();
        auto expectedPrevHash = block.is_genesis() ? lastGenesis->getHash() : prevBlock->getHash();
        if (checkedPrevHash != expectedPrevHash) {
            // jestko
            eLog("[Blockchain] Can't chained, sync request");
            // eLog("jb {}", block);
            // if (lastBlock.has_value())
            //     eLog("lb {}", lastBlock.value());
            // if (lastGenesis.has_value())
            //     eLog("lg {}", lastGenesis.value());
            // if (block.getType() != BlockType::Dummy)

            remove_last_block();

            // TODO: hashs incoming, not sync, remove block
            // TODO: package for removing last block?
            start_sync();
            // sync(blockId - 1); // TODO: request only chel
            // who sended block?
            return std::unexpected(BlockError::InvalidHash);
        }
    }

    if (block.getType() == BlockType::Genesis) {
        // eLog("[Blockchain] Adding a genesis block {} to
        // storage", block.id());
    } else {
        // eLog("[Blockchain] Adding a block {} to storage
        // {}", block.id(), block.getType());
    }

    this->update_network_id(block);

    // check hash...

    // const auto           &transactions =
    // block.transactions(); std::set<Transaction>
    // transactions_approved;
    // // TODO: if remove tx -> ignore in prove
    // for (const auto &tx : block.transactions()) {
    //     auto res = proveTransaction(tx, transactions);

    //     if (res == TransactionProveError::NoError || res
    //     == TransactionProveError::SelfPleasure) {
    //         transactions_approved.insert(tx);
    //     }
    // }

    // recalc hash

    BlockVariant newBlock(block);
    // newBlock.set_transactions(transactions_approved);

    if (block.getType() != BlockType::Dummy && block.id() != BigNumber(0)) {
        signBlock(newBlock);
    }

    // TIMER_START(BlockIndexAdd)
    const auto res = blockIndex.addBlock(newBlock);
    // TIMER_END(BlockIndexAdd)

    const auto blockType = newBlock.getType();

    if (!res.has_value()) {
        if (res.error() == BlockError::AlreadyExists && newBlock.id() > 0) {
            if (newBlock.getType() == BlockType::Dummy) {
                return res;
            }

            // if sign.count < ... -> ?
            if (res.error() == BlockError::Equal) {
                return newBlock;
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
    blockIndex.removeById(block.id());
    // remove_last_block();
    return addBlock(block);
}

int Blockchain::removeBlock(const BlockVariant &block) {
    return blockIndex.removeById(block);
}

bool Blockchain::canMergeBlocks(const BlockVariant &receivedBlock, const BlockVariant &existedBlock) {
    // 1) Blocks are approved
    // 2) Blocks has one type
    // 3) Blocks ids are identical
    if (receivedBlock.signatures().empty() || existedBlock.signatures().empty()
        || receivedBlock.getType() != existedBlock.getType() || receivedBlock.id() != existedBlock.id()) {
        return false;
    }

    return true;
}

std::expected<BlockVariant, BlockError> Blockchain::mergeBlocks(const Block &blockA, const Block &blockB) {
    // eLog("[Blockchain] Attempting to merge {} and {}",
    // blockA, blockB);

    if (blockA.id() == 0) {
        return std::unexpected(BlockError::CantMerge);
    }

    auto prev = read_block_by_id(blockA.id() - 1);
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

    if (blockA.id() == 0) {
        return std::unexpected(BlockError::CantMerge);
    }

    auto prev = read_block_by_id(blockA.id() - 1);
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
    return blockIndex.getLastSavedId();
}

BigNumber Blockchain::getCountRealBlockRecords() const {
    return BigNumber(0);
}

int Blockchain::getCountTransactionsInBlocks() const {
    return 0;
}

BigNumberFloat Blockchain::calculate_actor_balance(const ActorId &actor_id,
                                                   const TokenId &token_id,
                                                   bool           ignore_genesis) const {
    eLog("calculate_actor_balance: {} for token {}", actor_id, token_id);
    if (blockIndex.getFirstSavedId() == -1 || blockIndex.getLastSavedId() == -1) {
        return BigNumberFloat(0);
    }

    BigNumberFloat balance;

    for (BigNumber i = this->blockIndex.getLastSavedId(); i >= blockIndex.getFirstSavedId(); i--) {
        auto currentBlock = blockIndex.read_block_by_id(i);

        if (!currentBlock.has_value()) {
            continue;
        }
        if (!currentBlock->isBlock() && !currentBlock->is_genesis()) {
            continue;
        }
        if (currentBlock->getType() == BlockType::Dummy) {
            continue;
        }

        if (ignore_genesis && currentBlock->is_genesis() && currentBlock->id() != BigNumber(0)) {
            continue;
        }

        if (currentBlock->is_genesis()) {
            if (ignore_genesis && currentBlock->id() != BigNumber(0)) {
                // if not mega
                continue;
            }

            auto       genesis = blockIndex.getGenesisBlockById(i);
            const auto rows    = genesis->dataRows();

            for (const auto &[key, row] : rows) {
                if (key.actorId == actor_id && key.tokenId == token_id)
                    balance += row.state;
            }

            return balance;
        }

        if (currentBlock->isEmpty()) {
            break;
        }

        // tx check
        auto txs = currentBlock->transactions();
        for (auto &tx : txs) {
            if (tx.type() == TransactionType::Reward && tx.sender() == actor_id && tx.token() == token_id) {
                balance += tx.amount();
                eLog("{} BAALANCE Reward += {}, = {}", i, tx.amount(), balance);
                continue;
            }

            if (tx.type() == TransactionType::InitContract && tx.sender() == actor_id && tx.token() == token_id) {
                balance += tx.amount();
                eLog("{} BAALANCE InitContract += {}, = {}", i, tx.amount(), balance);
                continue;
            }

            if (tx.type() == TransactionType::Conversion && tx.sender() == actor_id) {
                auto from_token = ActorId::create(tx.data());
                if (!from_token.has_value()) {
                    continue;
                }

                if (from_token.value() == tx.token()) {
                    continue;
                }

                if (from_token.value() == token_id) {
                    balance -= tx.amount();
                    eLog(
                        "{} BAALANCE Conversion -= {}, = "
                        "{}",
                        i,
                        tx.amount(),
                        balance);
                }

                if (tx.token() == token_id) {
                    balance += tx.amount();
                    eLog(
                        "{} BAALANCE Conversion += {}, = "
                        "{}",
                        i,
                        tx.amount(),
                        balance);
                }
                continue;
            }

            if (tx.receiver() == actor_id && tx.token() == token_id) {
                balance += tx.amount();
                eLog("{} BAALANCE += {}, = {}", i, tx.amount(), balance);
            }

            if (tx.sender() == actor_id && tx.token() == token_id) {
                balance -= tx.amount();
                eLog("{} BAALANCE -= {}, = {}", i, tx.amount(), balance);
            }
        }
    }

    return balance;
}

std::unordered_map<ActorId, BigNumberFloat> Blockchain::calculate_actors_balance(
    const std::vector<ActorId> &actor_ids,
    const TokenId              &token_id,
    bool                        ignore_genesis) const {
    std::unordered_map<ActorId, BigNumberFloat> balances;

    eLog("calculate_actorS_balance: {} for token {}", actor_ids, token_id);
    if (blockIndex.getFirstSavedId() == -1 || blockIndex.getLastSavedId() == -1) {
        for (const auto &actor_id : actor_ids) {
            balances[actor_id] = BigNumberFloat(0);
        }
        return balances;
    }

    for (BigNumber i = this->blockIndex.getLastSavedId(); i >= blockIndex.getFirstSavedId(); i--) {
        auto currentBlock = blockIndex.read_block_by_id(i);

        if (!currentBlock.has_value()) {
            continue;
        }
        if (!currentBlock->isBlock() && !currentBlock->is_genesis()) {
            continue;
        }
        if (currentBlock->getType() == BlockType::Dummy) {
            continue;
        }

        if (ignore_genesis && currentBlock->is_genesis() && currentBlock->id() != BigNumber(0)) {
            continue;
        }

        if (currentBlock->is_genesis()) {
            if (ignore_genesis && currentBlock->id() != BigNumber(0)) {
                // if not mega
                continue;
            }

            // eLog("{} BAALANCE Genesis", i);
            auto       genesis = blockIndex.getGenesisBlockById(i);
            const auto rows    = genesis->dataRows();

            for (const auto &[key, row] : rows) {
                for (const auto &actor_id : actor_ids) {
                    if (key.actorId == actor_id && key.tokenId == token_id)
                        balances[actor_id] += row.state;
                }
            }

            return balances;
        }

        if (currentBlock->isEmpty()) {
            break;
        }

        // tx check
        auto txs = currentBlock->transactions();
        for (auto &tx : txs) {
            for (const auto &actor_id : actor_ids) {
                if (tx.type() == TransactionType::Reward && tx.sender() == actor_id && tx.token() == token_id) {
                    balances[actor_id] += tx.amount();
                    // eLog("{} BAALANCE Reward += {}, =
                    // {}", i, tx.amount(),
                    // balances[actor_id]);
                    continue;
                }

                if (tx.type() == TransactionType::InitContract && tx.sender() == actor_id
                    && tx.token() == token_id) {
                    balances[actor_id] += tx.amount();
                    // eLog("{} BAALANCE InitContract += {},
                    // = {}", i, tx.amount(),
                    // balances[actor_id]);
                    continue;
                }

                if (tx.type() == TransactionType::Conversion && tx.sender() == actor_id) {
                    auto from_token = ActorId::create(tx.data());
                    if (!from_token.has_value()) {
                        continue;
                    }

                    if (from_token.value() == tx.token()) {
                        continue;
                    }

                    if (from_token.value() == token_id) {
                        balances[actor_id] -= tx.amount();
                        // eLog("{} BAALANCE Conversion -=
                        // {}, = {}", i, tx.amount(),
                        // balances[actor_id]);
                    }

                    if (tx.token() == token_id) {
                        balances[actor_id] += tx.amount();
                        // eLog("{} BAALANCE Conversion +=
                        // {}, = {}", i, tx.amount(),
                        // balances[actor_id]);
                    }
                    continue;
                }

                if (tx.receiver() == actor_id && tx.token() == token_id) {
                    balances[actor_id] += tx.amount();
                    // eLog("{} BAALANCE += {}, = {}", i,
                    // tx.amount(), balances[actor_id]);
                }

                if (tx.sender() == actor_id && tx.token() == token_id) {
                    balances[actor_id] -= tx.amount();
                    // eLog("{} BAALANCE -= {}, = {}", i,
                    // tx.amount(), balances[actor_id]);
                }
            }
        }
    }

    return balances;
}

void Blockchain::showBlockchain() const {
    eLog("[Blockchain] Show blockchain:");

    auto genBlock = blockIndex.getLastGenesisBlock();
    // eLog("Last genesis: {}", genBlock);

    int i = 0;

    // do {
    //     auto currentBlock = blockIndex.getBlockById(i);

    //     if (currentBlock->isGenesisBlock())
    //         eLog("{}",
    //         currentBlock->getGenesisBlockConst());
    //     else
    //         eLog("{}", currentBlock->getBlock());

    //     i++;
    // } while (!currentBlock.isEmpty());
}

BigNumber Blockchain::getBlockCount() {
    // eLog("[Blockchain] Count: {}",
    // this->blockIndex.getLastSavedId());
    return this->blockIndex.getLastSavedId();
}

std::expected<BlockVariant, BlockError> Blockchain::addBlockNetwork(const BlockVariant         &block,
                                                                    const Responder            &responder,
                                                                    const NetworkPackageStorage package,
                                                                    bool                        resend) {
    TIMER_START(addBlockNetwork)
    if (status_ == BlockchainStatus::Sync && block.id() != BigNumber(0)) {
        //
        // eLog("[Blockchain] Ignore add block {}, because blockhain sync", block.id());
        node->network()->sendBrodcastMessageFurther(package);
        return std::unexpected(BlockError::BlockchainBusy);
    }

    if (block.id() > 0 && block.is_genesis()) {
        // eLog("!!!!!!!!!!!");
    }
    if (block.getType() == BlockType::Data) {
        // eLog("???????");
    }

    auto lastBlock = this->read_last_block();
    if (block.id() != 0 && (!lastBlock.has_value() || (lastBlock.has_value() && block.isEmpty()))) {
        return std::unexpected(BlockError::EmptyBlockchain);
    }

    auto res = addBlock(block);

    if (!res.has_value()) {
        switch (res.error()) {
        case BlockError::AlreadyChained: {
            // if (blockIndex.lastSavedId - 100 <=
            // block.id() &&
            // !responder.identifiers().empty()) {
            //     // syncResponse(block.id(), responder);
            //  } // else {
            //      node->network()->send_message("",
            //                                    MessageType::BlockchainAnarchy,
            //                                    MessageStatus::Response,
            //                                    messageId,
            //                                    SendMode::Focused);
            //  }

            return res;
        }
        default:
            break;
        }

        return res;
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
    // sendBlockByNumber(block.id());

    // notifications for clients
    if (res->getType() != BlockType::Data) {
        return res;
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
                emit updateSelf(block.id());

                emit transaction_cache_.add(block.id(), block.getDate(), transaction);

#ifdef IS_R
                if (transaction.type() == TransactionType::Reward
                    && accountId == node->accountController()->mainActor().id()) {
                    Transaction tx;
                    tx.setSender(accountId);
                    tx.setReceiver(accountId);
                    tx.setType(TransactionType::Conversion);
                    tx.setData(ActorId().to_string());
                    tx.setAmount(transaction.amount());
                    tx.setPrevBlock(block.id());
                    tx.setToken(
                        ActorId("468faf2f1be6504a9a26f7f027"
                                "f7e43380b0d77d"));
                    eLog(
                        "[Reward] Send conversion: {} "
                        "coins",
                        tx.amount());
                    node->sendTransaction(tx, node->accountController()->mainActor());
                }
#endif
            }
        }
    }

    return res;

    // if (list.contains(tmp.getSender())) {
    //     emit newNotify({
    //     QDateTime::currentMSecsSinceEpoch(),
    //                      Notification::NotifyType::TxToUser,
    //                      tmp.getReceiver().toByteArray()
    //                      });
    // } else if (list.contains(tmp.getReceiver())) {
    //     emit newNotify({
    //     QDateTime::currentMSecsSinceEpoch(),
    //                      Notification::NotifyType::TxToMe,
    //                      tmp.getSender().toByteArray()
    //                      });
    // }
    TIMER_END(addBlockNetwork)
}

// Actors //
TransactionProveError Blockchain::prove_transaction(const Transaction          &tx,
                                                    const std::set<Transaction> transactions) {
    // eLog("[Blockchain] Transaction prove started: {}",
    // tx);
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

    auto res = this->blockIndex.search_duplicate(tx_copy.hash());
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

    if (tx.type() == TransactionType::Reward || tx.type() == TransactionType::InitContract
        || tx.type() == TransactionType::Conversion) {
        if (targetSender != targetReceiver) {
            return TransactionProveError::NotIdenticalSenderReceiver;
        }
    } else {
        if (targetSender == targetReceiver) {
            return TransactionProveError::IdenticalSenderReceiver;
        }
    }

    auto block = read_last_block();
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

    // special conditions: receiver is null - coins burning,
    // contract creation
    // TODO: InitContract: check duplicate
    if (tx.type() == TransactionType::InitContract) {
        auto count = tx.amount();
        if (count < 0 || count >= Token::MAX_TOKEN_COUNT) {
            return TransactionProveError::InvalidTokenCount;
        }

        return TransactionProveError::NoError;
    }

    if (tx.type() == TransactionType::Conversion) {
        return TransactionProveError::NoError;
    }

    TokenId token = tx.token();
    if (tx.type() == TransactionType::Conversion) {
        auto from_token = TokenId::create(tx.data());
        if (!from_token.has_value()) {
            return TransactionProveError::ConversionIncorrectFromToken;
        }

        token = from_token.value();

        if (from_token == tx.token()) {
            return TransactionProveError::ConversionEqualToken;
        }
    }

    return TransactionProveError::NoError;

    BigNumberFloat transactionAmount = tx.amount();
    BigNumberFloat senderBalance     = calculate_actor_balance(targetSender, token);

    // tx check
    for (const Transaction &tx_check : std::as_const(transactions)) {
        if (tx.hash() == tx_check.hash()) {
            continue;
        }

        if (tx_check.token() != token) {
            continue;
        }

        if (tx_check.type() == TransactionType::Reward && tx_check.sender() == tx_check.receiver()
            && tx_check.token() == token) {
            senderBalance += tx_check.amount();
            continue;
        }

        if (tx_check.type() == TransactionType::InitContract && tx_check.sender() == tx_check.receiver()
            && tx_check.token() == token) {
            senderBalance += tx_check.amount();
            continue;
        }

        if (tx_check.type() == TransactionType::Conversion && tx_check.sender() == tx_check.receiver()) {
            if (tx_check.data() == token.to_string()) {
                senderBalance -= tx_check.amount();
            }
            if (tx_check.token() == token) {
                senderBalance += tx_check.amount();
            }
            continue;
        }

        if (tx_check.sender() == targetSender && tx_check.token() == token) {
            senderBalance -= tx_check.amount();
        }

        if (tx_check.receiver() == targetReceiver && tx_check.token() == token) {
            senderBalance += tx_check.amount();
        }
    }

    if (senderBalance < transactionAmount) {
        return TransactionProveError::SenderBalanceBelowZero;
    }

    return TransactionProveError::NoError;
}

void Blockchain::process() {
    connect(this, &Blockchain::need_check, [this] {
        this->start_check();
    });
    connect(this, &Blockchain::addBlockFromNetwork, this, &Blockchain::addBlockNetwork);
    connect(this, &Blockchain::syncResponseFromNetwork, this, &Blockchain::syncResponse);
    connect(this, &Blockchain::syncResponseVectorFromNetwork, this, &Blockchain::syncResponseVector);
    connect(this, &Blockchain::network_status_sync_request_signal, this, &Blockchain::network_status_sync_request);
    connect(this,
            &Blockchain::network_status_sync_response_signal,
            this,
            &Blockchain::network_status_sync_response);

    timer_sync = new QTimer(this);
    connect(timer_sync, &QTimer::timeout, this, &Blockchain::timer_sync_tick);
    connect(this, &Blockchain::removeAll, this, &Blockchain::removeAllSlot);
}

// Other //

BlockIndex &Blockchain::getBlockIndex() {
    return blockIndex;
}

void Blockchain::removeAllSlot(bool is_mega, bool is_exit) {
#ifndef IS_R
    if (!is_mega) {
        return;
    }
#endif

    eLog("[Blockchain] Remove all...");
    this->blockIndex.removeAll();
    QFile(QString::fromStdString(BlockchainConst::TMP_GENESIS_BLOCK)).remove();
    eLog("[Blockchain] Removed all");

#ifdef IS_RC
    if (is_exit) {
        qApp->exit();
    }
#endif
}

void Blockchain::timer_sync_tick() {
    eLog("[Blockchain] Timer sync tick");
    timer_sync->stop();
    status_ = BlockchainStatus::Timered;
    emit statusChanged(status_);
    start_sync();
}

BlockchainMode Blockchain::getMode() const {
    return mode;
}

void Blockchain::setMode(BlockchainMode newMode) {
    mode = newMode;
}
