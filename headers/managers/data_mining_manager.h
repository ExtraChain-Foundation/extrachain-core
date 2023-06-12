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

#ifndef DATA_MINING_MANAGER_H
#define DATA_MINING_MANAGER_H

#include "utils/bignumber_float.h"
#include <QObject>
#include <managers/extrachain_node.h>
#include <network/message_body.h>
#include <string>
#include <utils/db_connector.h>
#include <utils/dfs_utils.h>

class DataMiningManager : public QObject {
    Q_OBJECT

    ExtraChainNode *node;
    const int CoinProductionRate = 100;
    const BigNumberFloat farmingPercent = BigNumberFloat("0.0002");
    BigNumberFloat balanceFarming;
    BigNumber indexBlockFarming = 2;//42300;
    BigNumber indexBlock;
    bool isFarmingCashEmpty = true;
    bool isRecalculate = false;

    const std::string farmingCachePath = DataStorage::BLOCKCHAIN_INDEX.toStdString() + "/"
        + DataStorage::ACTOR_INDEX_FOLDER_NAME.toStdString() + "/farming";

public:
    DataMiningManager(ExtraChainNode *node, QObject *parent = nullptr);

    BigNumberFloat calculateCoins(BigNumberFloat dataAmountStored,
                                  BigNumberFloat dataAmountTotalStoredInNetwork,
                                  BigNumberFloat circulativeSupply, BigNumberFloat blockAmount,
                                  double coefficient);
    Transaction makeRewardTx(const MessageBody &state);
    Transaction makeRewardTx(const DFSR::RequestReward &requestReward, const double coefficient = 0.5);

    void coinRewardRequest(const BigNumber &blockIndex);
    void interestAccrual();
    BigNumberFloat farmingBalance() const;
    void calculateFarmingBalanceMainUser();

private:
    void updateLastIndex();
};

#endif // DATA_MINING_MANAGER_H
