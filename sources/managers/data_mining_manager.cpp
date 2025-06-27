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
#include "chain/dag.h"
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
#ifndef IS_RC
    return;
#endif
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID) && !defined(RACCOON_CLIENT_CONSOLE)
    return;
#endif

    if (node->accountController()->empty()) {
        return;
    }

    if (node->dag()->status() != DagStatus::Ready) {
        return;
    }

    const auto actor      = node->accountController()->system_actor();
    auto       totalBytes = node->network()->getCalculateTraffic()->totalBytes();
    auto       amount     = calculateRewardAmount();

    // eLog("[Reward] Request: Actor: {}, Dfs size: {}, Reward: {}, Traffic sent/received: {}/{}, Blocks: {}",
    //      actor.id(),
    //      node->dfs()->sizeTaken(),
    //      amount,
    //      totalBytes.first,
    //      totalBytes.second,
    //      node->dag count stored);

    if (amount <= 0) {
        eLog("[Reward] Can't send amount, because amount = 0");
        return;
    }

    amount.truncate();
    if (amount > MaxReward) {
        amount = MaxReward;
    }

    Transaction transaction;
    transaction.set_sender(actor.id());
    transaction.set_receiver(actor.id());
    transaction.set_amount(amount);
    transaction.set_type(TransactionType::Reward);
    // transaction.set_section(node->dag()->current_section() + 1);
    // transaction.sign(actor);

    auto tx_result = node->dag()->prepare_transaction(transaction, actor);
    if (!tx_result.has_value()) {
        eLog("[Reward] Can't send amount, because can't prepare transaction: {}", tx_result.error());
        return;
    }

    Transaction tx_conv;
    tx_conv.set_sender(actor.id());
    tx_conv.set_receiver(actor.id());
    tx_conv.set_type(TransactionType::Conversion);
    tx_conv.set_meta(ActorId().to_string());
    tx_conv.set_amount(transaction.amount());
    tx_conv.set_token(
        ActorId("468faf2f1be6504a9a26f7f027"
                "f7e43380b0d77d"));

    auto tx_result2 = node->dag()->prepare_transaction(tx_conv, actor);
    if (!tx_result2.has_value()) {
        eLog("[Reward] Can't send amount, because can't prepare transaction: {}", tx_result.error());
        return;
    }

    auto requestReward = Dfs::Reward::RequestReward { .DataStoredSize     = node->dfs()->sizeTaken(),
                                                      .TypeFunctioningObj = Dfs::Reward::Base,
                                                      .BytesSent          = totalBytes.first,
                                                      .BytesReceived      = totalBytes.second,
                                                      .BlocksStored       = node->dag()->current_section(),
                                                      .transaction        = tx_result.value(),
                                                      .convert            = tx_result2.value() };

    node->dag()->add_transaction_sended(tx_result.value());
    node->dag()->add_transaction_sended(tx_result2.value());

    node->network()->send_message(requestReward,
                                  MessageType::CoinReward,
                                  SendMode::Broadcast,
                                  MessageStatus::Request);

    auto data_serialized = MessagePack::serialize(requestReward);
    auto des             = MessagePack::deserialize<Dfs::Reward::RequestReward>(data_serialized);

    eLog("[Reward] Sended {}", requestReward);

    // if (requestReward.transaction != des->transaction) {
    // eFatal("Reward error");
    // }
}

BigNumberFloat DataMiningManager::calculateRewardAmount() const {
    // (dataStoredSize/dfsSize + bytesReceived/BytesSent)+(sectionsStoredSize/dagSize) * k (k=100)
    // node->dfs()->refresh_calculate();
    const auto &totalBytes = node->network()->getCalculateTraffic()->totalBytes();

    if (totalBytes.first == 0 || node->dfs()->totalDfsSize() == 0) {
        // eLog("[Reward] Request calculation: return amount 0. TotalBytes: {}, total dfs: {}",
        //      totalBytes.first,
        //      node->dfs()->totalDfsSize());
        return BigNumberFloat(0);
    }

    auto current_section = BigNumberFloat(node->dag()->current_section());
    if (current_section == BigNumberFloat(0)) {
        current_section = BigNumberFloat(1);
    }

    auto sizeTaken        = BigNumberFloat(node->dfs()->sizeTaken());
    auto totalDfsSize     = BigNumberFloat(node->dfs()->totalDfsSize());
    auto totalBytesFirst  = BigNumberFloat(totalBytes.first);
    auto totalBytesSecond = BigNumberFloat(totalBytes.second);
    // auto sectionsStored     =
    auto res = sizeTaken / totalDfsSize + 1 / 1 // totalBytesSecond / totalBytesFirst
               + BigNumberFloat(node->dag()->current_section()) / current_section * 100;

    if (node->dfs()->mode() == DfsMode::Full) {
        res *= KoefRewardDagDfs * koef_to_koef;
    } else if (node->dag()->mode() == DagMode::Full) {
        res *= KoefRewardDag * koef_to_koef;
    } else {
        res *= KoefReward * koef_to_koef;
    }

    // res *= node->dag()->mode() != DagMode::Light ? KoefRewardDag * koef_to_koef : KoefReward * koef_to_koef;

    // eLog(
    //     "[Reward] Request calculation: Dfs ratio: {}/{}, Traffic ratio: {}/{}, Sections ratio: {}/{},
    //     Multiplier: , " "Result: {}" "100", sizeTaken, totalDfsSize, totalBytesSecond, totalBytesFirst,
    //     sextionsStored, lastIndex, res);

    return res;
}

BigNumberFloat DataMiningManager::calculateRewardAmount(const Dfs::Reward::RequestReward &requestReward) const {
    if (requestReward.BytesSent == 0 || node->dfs()->totalDfsSize() == 0) {
        // eLog("{} {} {}", "[Reward] Cannot calculate reward due to division by zero. BytesSent, total
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

    auto current_section = BigNumberFloat(node->dag()->current_section());
    if (current_section == BigNumberFloat(0)) {
        current_section = BigNumberFloat(1);
    }

    auto res = (BigNumberFloat { requestReward.DataStoredSize } / node->dfs()->totalDfsSize()
                + 1 / 1 // + BigNumberFloat { requestReward.BytesReceived } / requestReward.BytesSent
                + BigNumberFloat { requestReward.BlocksStored } / current_section * 100);
    res *= KoefReward;
    return res;
}

bool DataMiningManager::network_request_coin_reward(const Dfs::Reward::RequestReward &requestReward,
                                                    const Responder                  &responder) {
    auto calc   = calculateRewardAmount(requestReward);
    auto amount = requestReward.transaction.amount();

    if (amount <= MaxReward || calc - amount <= Dfs::Reward::TOLERANCE) {
        if (requestReward.transaction.sender() != requestReward.transaction.receiver()) {
            return false;
        }

        // eLog("[Reward] Add request: {}", requestReward);
        auto res1 = node->dag()->network_transaction(requestReward.transaction, responder);
        auto res2 = node->dag()->network_transaction(requestReward.convert, responder);

        if (!res1.has_value() || !res2.has_value()) {
            return false;
        }

        return true;
    } else {
        return false;
        // eLog("[Reward] Can't add request: {}, calc: {}, amount: {}", requestReward, calc, amount);
    }
}

void DataMiningManager::set_koef_to_koef(const BigNumberFloat &koef_to_koef) {
    this->koef_to_koef = koef_to_koef;
}
