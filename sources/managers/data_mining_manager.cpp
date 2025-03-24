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
#include "blockchain/dag.h"
#include "dfs/dfs_utils.h"
#include "managers/account_controller.h"
#include "dfs/dfs_controller.h"
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
#if defined(Q_OS_LINUX) && !defined(RACCOON_CLIENT_CONSOLE)

#endif

#ifndef IS_RC
    return; // temp
#endif

    if (node->accountController()->empty()) {
        return;
    }
    // if (node->blockchain()->status() != BlockchainStatus::Ready) {
    //     return;
    // }

    const auto actor      = node->accountController()->system_actor();
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
        eLog("[Reward] Can't send amount, because amount = 0");
        return;
    }

    Transaction transaction;
    transaction.setSender(actor.id());
    transaction.setReceiver(actor.id());
    transaction.setAmount(amount);
    transaction.setType(TransactionType::Reward);
    // transaction.set_section(node->dag()->current_section() + 1);
    // transaction.sign(actor);
    auto tx_result = node->dag()->prepare_transaction(transaction, actor);
    if (!tx_result.has_value()) {
        eLog("[Reward] Can't send amount, because can't prepare transaction: {}", tx_result.error());
        return;
    }
    auto tx = tx_result.value();

    auto requestReward = Dfs::Reward::RequestReward { .DataStoredSize     = node->dfs()->sizeTaken(),
                                                      .TypeFunctioningObj = Dfs::Reward::Base,
                                                      .BytesSent          = totalBytes.first,
                                                      .BytesReceived      = totalBytes.second,
                                                      .BlocksStored       = node->dag()->current_section(),
                                                      .transaction        = tx };

    node->dag()->add_transaction_sended(tx);

    node->network()->send_message(requestReward,
                                  MessageType::CoinReward,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);

    auto data_serialized = MessagePack::serialize(requestReward);
    auto des             = MessagePack::deserialize<Dfs::Reward::RequestReward>(data_serialized);

    eLog("[Reward] Sended {}", requestReward);
}

BigNumberFloat DataMiningManager::calculateRewardAmount() const {
    // (dataStoredSize/dfsSize + bytesReceived/BytesSent)+(blocksStoredSize/blockchainSize) * k (k=100)
    node->dfs()->refresh_calculate();
    const auto &totalBytes = node->network()->getCalculateTraffic()->totalBytes();

    if (totalBytes.first == 0 || node->dfs()->totalDfsSize() == 0) {
        // eLog("[Reward] Request calculation: return amount 0. TotalBytes: {}, total dfs: {}",
        //      totalBytes.first,
        //      node->dfs()->totalDfsSize());
        return BigNumberFloat(0);
    }

    // auto lastBlock = node->blockchain()->read_last_block();
    // if (!lastBlock.has_value())
    //     return BigNumberFloat(0);
    // if (lastBlock->isEmpty())
    //     return BigNumberFloat(0);
    // auto lastIndex = lastBlock->id();
    // if (lastIndex == BigNumber(0)) {
    //     lastIndex = BigNumber(1);
    //     // return BigNumberFloat(0);
    // }

    auto sizeTaken        = BigNumberFloat(node->dfs()->sizeTaken());
    auto totalDfsSize     = BigNumberFloat(node->dfs()->totalDfsSize());
    auto totalBytesFirst  = BigNumberFloat(totalBytes.first);
    auto totalBytesSecond = BigNumberFloat(totalBytes.second);
    // auto blocksStored     = BigNumberFloat(node->blockchain()->getBlocksStored());
    auto res = sizeTaken / totalDfsSize + totalBytesSecond / totalBytesFirst
               + (BigNumberFloat(1) / BigNumberFloat(1) * 100);
    res *= KoefReward;

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

    // auto lastBlock = node->blockchain()->read_last_block();
    // if (!lastBlock.has_value())
    //     return BigNumberFloat(0);
    // if (lastBlock->isEmpty())
    //     return BigNumberFloat(0);
    // auto lastIndex = lastBlock->id();
    // if (lastIndex == 0) { // a u jk
    //     lastIndex = BigNumber(1);
    //     // return BigNumberFloat(0);
    // }

    auto res = (BigNumberFloat { requestReward.DataStoredSize } / node->dfs()->totalDfsSize()
                + BigNumberFloat { requestReward.BytesReceived } / requestReward.BytesSent
                + (BigNumberFloat { requestReward.BlocksStored } / BigNumberFloat(1) * 100));
    res *= KoefReward;
    return res;
}

void DataMiningManager::network_request_coin_reward(const Dfs::Reward::RequestReward &requestReward,
                                                    const Responder                  &responder) {
    auto calc   = calculateRewardAmount(requestReward);
    auto amount = requestReward.transaction.amount();

    // * KoefReward
    if (calc - amount <= Dfs::Reward::TOLERANCE) {
        if (requestReward.transaction.sender() != requestReward.transaction.receiver()) {
            return;
        }

        // eLog("[Reward] Add request: {}", requestReward);
        node->dag()->network_transaction(requestReward.transaction, responder);
    } else {
        // eLog("[Reward] Can't add request: {}, calc: {}, amount: {}", requestReward, calc, amount);
    }
}
