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

#include "managers/data_mining_manager.h"
#include "datastorage/blockchain.h"
#include "datastorage/dfs/dfs_controller.h"
#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

DataMiningManager::DataMiningManager(ExtraChainNode *node, QObject *parent)
    : QObject(parent) {
    this->node = node;
}

BigNumberFloat DataMiningManager::calculateCoins(BigNumberFloat dataAmountStored,
                                                 BigNumberFloat dataAmountTotalStoredInNetwork,
                                                 BigNumberFloat circulativeSupply, BigNumberFloat blockAmount,
                                                 double coefficient) {
    if (dataAmountStored == 0 || dataAmountTotalStoredInNetwork == 0 || circulativeSupply == 0
        || blockAmount == 0) {
        return BigNumberFloat();
    }
    BigNumberFloat coinProducedForNode(0);
    coinProducedForNode =
        (dataAmountStored / dataAmountTotalStoredInNetwork) * (circulativeSupply / blockAmount) * coefficient;

    if (coinProducedForNode < 1) {
        coefficient *= 2;
        coinProducedForNode = calculateCoins(dataAmountStored, dataAmountTotalStoredInNetwork,
                                             circulativeSupply, blockAmount, coefficient);
    }
    return coinProducedForNode;
}

Transaction DataMiningManager::makeRewardTx(const MessageBody &mb) {
    DFSP::StateMessage state = MessagePack::deserialize<DFSP::StateMessage>(mb.data);
    BigNumberFloat circulativeSupply(node->blockchain()->getCirculativeSuply().toStdString(10));
    BigNumberFloat blockAmount(node->blockchain()->getRecords().toStdString(10));
    BigNumberFloat dataAmountStoredInNetwork(std::to_string(node->dfs()->totalDfsSize()));

    qDebug() << "circulativeSupply" << circulativeSupply << "blockAmount" << blockAmount
             << "dataAmountStoredInNetwork" << dataAmountStoredInNetwork << "dataAmountStored"
             << state.DataAmountStored << "coef" << state.Coefficient;

    BigNumberFloat result = calculateCoins(BigNumberFloat(state.DataAmountStored), dataAmountStoredInNetwork,
                                           circulativeSupply, blockAmount, state.Coefficient);
    qDebug() << "result: " << result;

    Transaction rewardTx;
    rewardTx.setAmount(result);
    rewardTx.setReceiver(mb.sender_id);
    rewardTx.setSender(node->actorIndex()->firstId());
    rewardTx.setTypeTx(TypeTx::RewardTransaction);
    rewardTx.setToken(ActorId());
    rewardTx.setPrevBlock(node->blockchain()->getLastRealBlock().getIndex());
    rewardTx.setData(Utils::bytesEncodeStdString(mb.data));
    rewardTx.sign(node->accountController()->mainActor());
    qDebug() << rewardTx.getTypeTx();
    return rewardTx;
}

void DataMiningManager::coinRewardRequest(const BigNumber &blockIndex) {
    if (blockIndex % CoinProductionRate == 0) {
        qDebug() << "Make reward request" << std::stoi(blockIndex.toStdString(10));
        DFSP::StateMessage stateMessage;
        stateMessage.DataAmountStored = node->dfs()->calculateDataAmountStored();
        node->network()->send_message(stateMessage, MessageType::DfsState, MessageStatus::Response,
                                      "234234234312345", Config::Net::TypeSend::All);
    }
}
