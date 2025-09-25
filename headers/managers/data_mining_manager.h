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

#pragma once

#include <string>
#include <QObject>

#include "dfs/dfs_controller.h"
#include "utils/bignumber_float.h"
#include "managers/extrachain_node.h"
#include "dfs/dfs_utils.h"

static const int MINING_TIMER_TICK = 60000;

class DataMiningManager : public QObject {
    Q_OBJECT

public:
    DataMiningManager(ExtraChainNode *node, QObject *parent = nullptr);

    /**
     * @brief calculateCoins
     * @param dataAmountStored
     * @param dataAmountTotalStoredInNetwork
     * @param circulativeSupply
     * @param blockAmount
     * @param coefficient
     * @return
     */
    BigNumberFloat calculateCoins(BigNumberFloat dataAmountStored,
                                  BigNumberFloat dataAmountTotalStoredInNetwork,
                                  BigNumberFloat circulativeSupply,
                                  BigNumberFloat blockAmount,
                                  double         coefficient);

    /**
     * @brief Reward request
     * */
    void requestCoinReward();

    /**
     * @brief calculate reward amound
     * @return amount of reward
     */
    BigNumberFloat calculate_reward_amount() const;
    BigNumberFloat calculate_reward_amount(const Dfs::Reward::RequestReward &request_reward) const;

    /**
     * @brief Send reward amount
     */
    bool network_request_coin_reward(const Dfs::Reward::RequestReward &request_reward, const Responder &responder);

    /**
     * @brief set_koef_to_koef
     * @param koef_to_koef
     */
    void set_koef_to_koef(const BigNumberFloat &koef_to_koef);

private:
    const int            max_reward_          = 2;
    const BigNumberFloat koef_reward_dag_dfs_ = BigNumberFloat("0.017", NumeralBase::Dec);
    const BigNumberFloat koef_reward_dag_     = BigNumberFloat("0.0063", NumeralBase::Dec); // 0.0063 - dfs + dag
    const BigNumberFloat koef_reward_         = BigNumberFloat("0.000015", NumeralBase::Dec);
    BigNumberFloat       koef_to_koef_        = BigNumberFloat(1);
    ExtraChainNode      *node;

    std::map<std::pair<ActorId, std::string>, std::uint64_t> last_reward_;
};
