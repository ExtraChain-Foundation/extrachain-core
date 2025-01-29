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

#include "managers/data_mining_manager.h"
#include "dfs/dfs_utils.h"
#include "managers/account_controller.h"
#include "blockchain/blockchain.h"
#include "dfs/dfs_controller.h"
#include "managers/transaction_manager.h"
#include "network/network_manager.h"
#include "utils/bignumber_float.h"

DataMiningManager::DataMiningManager(ExtraChainNode *node, QObject *parent)
    : QObject(parent) {
    this->node = node;
}

BigNumberFloat DataMiningManager::calculateCoins(BigNumberFloat dataAmountStored,
                                                 BigNumberFloat dataAmountTotalStoredInNetwork,
                                                 BigNumberFloat circulativeSupply,
                                                 BigNumberFloat blockAmount,
                                                 double         coefficient) {
    if (dataAmountStored == 0 || dataAmountTotalStoredInNetwork == 0 || circulativeSupply == 0
        || blockAmount == 0) {
        return BigNumberFloat();
    }
    BigNumberFloat coinProducedForNode(0);
    coinProducedForNode =
        (dataAmountStored / dataAmountTotalStoredInNetwork) * (circulativeSupply / blockAmount) * coefficient;

    if (coinProducedForNode < 1) {
        coefficient *= 2;
        coinProducedForNode = calculateCoins(dataAmountStored,
                                             dataAmountTotalStoredInNetwork,
                                             circulativeSupply,
                                             blockAmount,
                                             coefficient);
    }
    return coinProducedForNode;
}

void DataMiningManager::requestCoinReward() {
#ifdef Q_OS_LINUX
    return;
#endif
    if (node->accountController()->empty()) {
        return;
    }

    const auto actor      = node->accountController()->mainActor();
    auto       totalBytes = node->network()->getCalculateTraffic()->totalBytes();
    auto       amount     = calculateRewardAmount();

    // eLog("[Reward] Request: Actor: {}, Dfs size: {}, Reward: {}, Traffic sent/received: {}/{}, Blocks: {}",
    //      actor.id(),
    //      node->dfs()->sizeTaken(),
    //      amount,
    //      totalBytes.first,
    //      totalBytes.second,
    //      node->blockchain()->getBlocksStored());

    if (amount <= 0) {
        // eLog("[Reward] Can't send amount, because amount = 0");
        return;
    }

    Transaction transaction;
    transaction.setSender(actor.id());
    transaction.setReceiver(actor.id());
    transaction.setAmount(amount);
    transaction.setType(TransactionType::Reward);

    auto lastRealBlock = node->blockchain()->getLastRealBlock();
    if (!lastRealBlock.has_value() || (lastRealBlock.has_value() && lastRealBlock->isEmpty())) {
        eLog("[Reward] No blocks");
        return;
    }

    BigNumber lastBlockId = lastRealBlock->getIndex();
    transaction.setPrevBlock(lastBlockId);
    transaction.sign(actor);

    auto requestReward = Dfs::Reward::RequestReward { .DataStoredSize     = node->dfs()->sizeTaken(),
                                                      .TypeFunctioningObj = Dfs::Reward::Base,
                                                      .BytesSent          = totalBytes.first,
                                                      .BytesReceived      = totalBytes.second,
                                                      .BlocksStored       = node->blockchain()->getBlocksStored(),
                                                      .transaction        = transaction };

    node->network()->send_message(requestReward,
                                  MessageType::BlockchainCoinReward,
                                  Config::Net::TypeSend::Neighbours,
                                  MessageStatus::Request);
}

BigNumberFloat DataMiningManager::calculateRewardAmount() const {
    // (dataStoredSize/dfsSize + bytesReceived/BytesSent)+(blocksStoredSize/blockchainSize) * k (k=100)
    const auto &totalBytes = node->network()->getCalculateTraffic()->totalBytes();

    if (totalBytes.first == 0 || node->dfs()->totalDfsSize() == 0) {
        // eLog("[Reward] Request calculation: return amount 0. TotalBytes: {}, total dfs: {}",
        //      totalBytes.first,
        //      node->dfs()->totalDfsSize());
        return BigNumberFloat(0);
    }

    auto lastBlock = node->blockchain()->getLastBlock();
    if (!lastBlock.has_value())
        return BigNumberFloat(0);
    if (lastBlock->isEmpty())
        return BigNumberFloat(0);
    auto lastIndex = lastBlock->getIndex();
    if (lastIndex == BigNumber(0)) {
        lastIndex = BigNumber(1);
        // return BigNumberFloat(0);
    }

    auto sizeTaken        = BigNumberFloat(node->dfs()->sizeTaken());
    auto totalDfsSize     = BigNumberFloat(node->dfs()->totalDfsSize());
    auto totalBytesFirst  = BigNumberFloat(totalBytes.first);
    auto totalBytesSecond = BigNumberFloat(totalBytes.second);
    auto blocksStored     = BigNumberFloat(node->blockchain()->getBlocksStored());
    auto res              = sizeTaken / totalDfsSize + totalBytesSecond / totalBytesFirst
               + (blocksStored / BigNumberFloat(lastIndex) * 100);

    // eLog(
    //     "[Reward] Request calculation: Dfs ratio: {}/{}, Traffic ratio: {}/{}, Blocks ratio: {}/{}, Multiplier:
    //     , " "Result: {}" "100", sizeTaken, totalDfsSize, totalBytesSecond, totalBytesFirst, blocksStored,
    //     lastIndex,
    //     res);

    return res;
}

BigNumberFloat DataMiningManager::calculateRewardAmount(const Dfs::Reward::RequestReward &requestReward) const {
    if (requestReward.BytesSent == 0 || node->dfs()->totalDfsSize() == 0) {
        // eLog("{} {} {}", "[Blockchain] Cannot calculate reward due to division by zero. BytesSent, total
        // dfs:"
        //, requestReward.BytesSent, node->dfs()->totalDfsSize());
        return BigNumberFloat(0);
    }

    auto lastBlock = node->blockchain()->getLastBlock();
    if (!lastBlock.has_value())
        return BigNumberFloat(0);
    if (lastBlock->isEmpty())
        return BigNumberFloat(0);
    auto lastIndex = lastBlock->getIndex();
    if (lastIndex == 0) { // a u jk
        lastIndex = BigNumber(1);
        // return BigNumberFloat(0);
    }

    return (BigNumberFloat { requestReward.DataStoredSize } / node->dfs()->totalDfsSize()
            + BigNumberFloat { requestReward.BytesReceived } / requestReward.BytesSent
            + (BigNumberFloat { requestReward.BlocksStored } / BigNumberFloat(lastIndex) * 100));
}

void DataMiningManager::network_request_coin_reward(const Dfs::Reward::RequestReward &requestReward) {
    if ((calculateRewardAmount(requestReward) - requestReward.transaction.amount()) <= Dfs::Reward::TOLERANCE) {
        if (requestReward.transaction.sender() != requestReward.transaction.receiver()) {
            return;
        }

        node->transactionManager()->addTransaction(requestReward.transaction);
    }
}
