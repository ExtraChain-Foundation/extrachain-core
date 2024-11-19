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
#include "utils/db_connector.h"
#include "dfs/dfs_utils.h"

class DataMiningManager : public QObject {
    Q_OBJECT

    ExtraChainNode *node;
    const int       CoinProductionRate = 100;
    bool            isRecalculate      = false;

public:
    DataMiningManager(ExtraChainNode *node, QObject *parent = nullptr);

    BigNumberFloat calculateCoins(
        BigNumberFloat dataAmountStored,
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
    BigNumberFloat calculateRewardAmount() const;
    BigNumberFloat calculateRewardAmount(const Dfs::Reward::RequestReward &requestReward) const;

    /**
     * @brief Send reward amount
     */
    void sendCoinsReward(const Dfs::Reward::RequestReward &requestReward);

private:
};
