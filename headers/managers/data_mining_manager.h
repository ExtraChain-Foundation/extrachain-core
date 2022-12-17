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

class DataMiningManager : public QObject {
    Q_OBJECT
private:
    ExtraChainNode *node;
    const int CoinProductionRate = 100;

public:
    DataMiningManager(ExtraChainNode *node, QObject *parent = nullptr);

    BigNumberFloat calculateCoins(BigNumberFloat dataAmountStored,
                                  BigNumberFloat dataAmountTotalStoredInNetwork,
                                  BigNumberFloat circulativeSupply, BigNumberFloat blockAmount,
                                  double coefficient);
    Transaction makeRewardTx(const MessageBody &mb);
    void coinRewardRequest(const BigNumber &blockIndex);
};

#endif // DATA_MINING_MANAGER_H
