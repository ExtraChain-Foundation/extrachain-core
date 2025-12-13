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

BigNumberFloat DataMiningManager::calculate_coins(BigNumberFloat data_amount_stored,
                                                  BigNumberFloat data_amount_total_stored_network,
                                                  BigNumberFloat circulative_supply,
                                                  BigNumberFloat block_amount,
                                                  double         coefficient) {
    if (data_amount_stored == 0 || data_amount_total_stored_network == 0 || circulative_supply == 0
        || block_amount == 0) {
        return BigNumberFloat();
    }
    BigNumberFloat coin_produced_for_node(0);
    coin_produced_for_node = (data_amount_stored / data_amount_total_stored_network)
                             * (circulative_supply / block_amount) * coefficient;

    if (coin_produced_for_node < 1) {
        coefficient *= 2;
        coin_produced_for_node = calculate_coins(data_amount_stored,
                                                 data_amount_total_stored_network,
                                                 circulative_supply,
                                                 block_amount,
                                                 coefficient);
    }

    return coin_produced_for_node;
}

void DataMiningManager::request_reward() {
#if !defined(IS_APP_UI_CLIENT) && !defined(RACCOON_CLIENT_CONSOLE)
    return;
#endif

    if (node->account_controller()->empty()) {
        return;
    }

    if (node->dag()->status() != DagStatus::Ready) {
        return;
    }

#if !defined(QT_DEBUG) && !defined(Q_OS_ANDROID) && !defined(Q_OS_IOS)
    if (node->dag()->mode() == DagMode::Light && node->dfs()->mode() == DfsMode::Light
        && koef_to_koef_ == BigNumberFloat(1)) {
        return;
    }
#endif

    const auto actor      = node->account_controller()->system_actor();
    auto       totalBytes = node->network()->calculate_traffic()->total_bytes();
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

    auto requestReward = Dfs::Reward::RequestReward { .data_stored_size = node->dfs()->sizeTaken(),
                                                      .bytes_sent       = totalBytes.first,
                                                      .bytes_received   = totalBytes.second,
                                                      .sections_stored  = node->dag()->current_section(),
                                                      .transaction      = tx_result.value() };

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
    const auto &totalBytes = node->network()->calculate_traffic()->total_bytes();

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
    if (request_reward.bytes_sent == 0 || node->dfs()->totalDfsSize() == 0) {
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

    auto res = (BigNumberFloat { request_reward.data_stored_size } / node->dfs()->totalDfsSize()
                + 1 / 1 // + BigNumberFloat { requestReward.BytesReceived } / requestReward.BytesSent
                + BigNumberFloat { request_reward.sections_stored } / current_section * 100);
    res *= koef_reward_;
    return res;
}

Task<bool> DataMiningManager::network_request_coin_reward(Dfs::Reward::RequestReward request_reward,
                                                          Responder                  responder) {
    auto calc   = calculate_reward_amount(request_reward);
    auto amount = request_reward.transaction.amount();

    if (amount <= max_reward_ || calc - amount <= Dfs::Reward::TOLERANCE) {
        if (request_reward.transaction.sender() != request_reward.transaction.receiver()) {
            eLog("Ignore reward, because tx sender != tx receiver, {} {}",
                 request_reward.transaction.sender(),
                 request_reward.transaction.receiver());
            co_return false;
        }

        if (request_reward.transaction.sender() != responder.node_id().actor_id) {
            eLog("Ignore reward, because tx sender != message sender");
            co_return false;
        }
        auto  sender      = NodeId { .actor_id        = request_reward.transaction.sender(),
                                     .node_identifier = responder.node_id().node_identifier };
        auto &network_map = last_reward_[sender.actor_id];
        auto  network_it  = network_map.find(sender.node_identifier);

        if (network_it != network_map.end()) {
            auto current_time = Utils::current_date_ms();
            auto time_diff_ms = current_time - network_it->second;
            if (time_diff_ms < 50000) {
#ifndef IS_APP_CLIENT
                eLog("[Reward] Ignore from {}, diff: {} ms", sender, time_diff_ms);
#endif
                co_return false;
            }
        } else if (network_map.size() >= 5) {
            auto current_time = Utils::current_date_ms();
            std::erase_if(network_map, [current_time](const auto &pair) {
                return current_time - pair.second > 90000;
            });

            if (network_map.size() >= 5) {
#ifndef IS_APP_CLIENT
                eLog("[Reward] Reject new node identifier, limit reached: {}", sender);
#endif
                co_return false;
            }
        }

        auto res1 = co_await node->dag()->network_transaction(request_reward.transaction, responder);
        if (!res1.has_value()) {
            if (res1.error() != TransactionProveError::TooSectionDiff) {
                co_return false;
            }
        }

        network_map[sender.node_identifier] = Utils::current_date_ms();

        co_return true;
    } else {
        co_return false;
        // eLog("[Reward] Can't add request: {}, calc: {}, amount: {}", requestReward, calc, amount);
    }
}

void DataMiningManager::set_koef_to_koef(const BigNumberFloat &koef_to_koef) {
    this->koef_to_koef_ = koef_to_koef;
}
