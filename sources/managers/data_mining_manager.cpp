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
#if !defined(IS_RC) && !defined(RACCOON_CLIENT_CONSOLE)
    return;
#endif

    if (node->account_controller()->empty()) {
        return;
    }

    if (node->dag()->status() != DagStatus::Ready) {
        return;
    }

#if !defined(QT_DEBUG) && !defined(Q_OS_ANDROID)
    if (node->dag()->mode() == DagMode::Light && node->dfs()->mode() == DfsMode::Light
        && koef_to_koef_ == BigNumberFloat(1)) {
        return;
    }
#endif

    const auto actor      = node->account_controller()->system_actor();
    auto       totalBytes = node->network()->calculate_traffic()->totalBytes();
    auto       amount     = calculate_reward_amount();

    // eLog("[Reward] Request: Actor: {}, Dfs size: {}, Reward: {}, Traffic sent/received: {}/{}, Blocks: {}",
    //      actor.id(),
    //      node->dfs()->sizeTaken(),
    //      amount,
    //      totalBytes.first,
    //      totalBytes.second,
    //      node->dag count stored);

    amount.truncate();

    if (amount <= 0) {
        // eLog("[Reward] Can't send amount, because amount = 0");
        // return;
        amount = BigNumberFloat("0.0011", NumeralBase::Dec);
    }

    if (amount > max_reward_) {
        amount = max_reward_;
    }

    Transaction transaction;
    transaction.set_sender(actor.id());
    transaction.set_receiver(actor.id());
    transaction.set_amount(amount);
    transaction.set_type(TransactionType::Reward);
    transaction.set_token(TokenId("468faf2f1be6504a9a26f7f027f7e43380b0d77d"));
    // transaction.set_section(node->dag()->current_section() + 1);
    // transaction.sign(actor);

    auto tx_result = node->dag()->prepare_transaction(transaction, actor);
    if (!tx_result.has_value()) {
        eLog("[Reward] Can't send amount, because can't prepare transaction: {}", tx_result.error());
        return;
    }

    auto requestReward = Dfs::Reward::RequestReward { .DataStoredSize     = node->dfs()->sizeTaken(),
                                                      .TypeFunctioningObj = Dfs::Reward::Base,
                                                      .BytesSent          = totalBytes.first,
                                                      .BytesReceived      = totalBytes.second,
                                                      .BlocksStored       = node->dag()->current_section(),
                                                      .transaction        = tx_result.value() };

    node->dag()->add_transaction_sended(tx_result.value());
    // node->dag()->add_transaction_sended(tx_result2.value());

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

BigNumberFloat DataMiningManager::calculate_reward_amount() const {
    // (dataStoredSize/dfsSize + bytesReceived/BytesSent)+(sectionsStoredSize/dagSize) * k (k=100)
    // node->dfs()->refresh_calculate();
    const auto &totalBytes = node->network()->calculate_traffic()->totalBytes();

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
        res *= koef_reward_dag_dfs_ * koef_to_koef_;
    } else if (node->dag()->mode() == DagMode::Full) {
        res *= koef_reward_dag_ * koef_to_koef_;
    } else {
        res *= koef_reward_ * koef_to_koef_;
    }

    // res *= node->dag()->mode() != DagMode::Light ? KoefRewardDag * koef_to_koef : KoefReward * koef_to_koef;

    // eLog(
    //     "[Reward] Request calculation: Dfs ratio: {}/{}, Traffic ratio: {}/{}, Sections ratio: {}/{},
    //     Multiplier: , " "Result: {}" "100", sizeTaken, totalDfsSize, totalBytesSecond, totalBytesFirst,
    //     sextionsStored, lastIndex, res);

    return res;
}

BigNumberFloat DataMiningManager::calculate_reward_amount(const Dfs::Reward::RequestReward &request_reward) const {
    if (request_reward.BytesSent == 0 || node->dfs()->totalDfsSize() == 0) {
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

    auto res = (BigNumberFloat { request_reward.DataStoredSize } / node->dfs()->totalDfsSize()
                + 1 / 1 // + BigNumberFloat { requestReward.BytesReceived } / requestReward.BytesSent
                + BigNumberFloat { request_reward.BlocksStored } / current_section * 100);
    res *= koef_reward_;
    return res;
}

bool DataMiningManager::network_request_coin_reward(const Dfs::Reward::RequestReward &request_reward,
                                                    const Responder                  &responder) {
    auto calc   = calculate_reward_amount(request_reward);
    auto amount = request_reward.transaction.amount();

    if (amount <= max_reward_ || calc - amount <= Dfs::Reward::TOLERANCE) {
        if (request_reward.transaction.sender() != request_reward.transaction.receiver()) {
            return false;
        }

        //
        auto sender_id      = request_reward.transaction.sender();
        auto last_reward_it = last_reward_.find(sender_id);

        if (last_reward_it != last_reward_.end()) {
            auto current_time = Utils::current_date_ms();
            auto time_diff_ms = current_time - last_reward_it->second;

            if (time_diff_ms < 55000) {
#ifndef IS_R
                eLog("[Reward] Ignore from {}, diff: {} ms", sender_id, time_diff_ms);
#endif
                return false;
            }
        }

        // eLog("[Reward] Add request: {}", requestReward);
        auto res1 = node->dag()->network_transaction(request_reward.transaction, responder);

        if (!res1.has_value()) {
            return false;
        }

        last_reward_[sender_id] = request_reward.transaction.timestamp(); // Utils::current_date_ms();

        return true;
    } else {
        return false;
        // eLog("[Reward] Can't add request: {}, calc: {}, amount: {}", requestReward, calc, amount);
    }
}

void DataMiningManager::set_koef_to_koef(const BigNumberFloat &koef_to_koef) {
    this->koef_to_koef_ = koef_to_koef;
}
