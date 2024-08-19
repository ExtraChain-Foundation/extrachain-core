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
#include "managers/tx_manager.h"
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
    rewardTx.setReceiver(node->accountController()->currentProfile().farmings()[0].id());
    rewardTx.setSender(node->actorIndex()->firstId());
    rewardTx.setTypeTx(TypeTx::RewardTransaction);
    rewardTx.setToken(ActorId());
    rewardTx.setPrevBlock(node->blockchain()->getLastRealBlock().getIndex());
    rewardTx.setData(Utils::bytesEncodeStdString(mb.data));
    rewardTx.sign(*node->accountController()->mainActor());
    qDebug() << rewardTx.getTypeTx();
    return rewardTx;
}

Transaction DataMiningManager::makeRewardTx(const DFS::Reward::RequestReward &requestReward,
                                            const double coefficient) {
    BigNumberFloat circulativeSupply(node->blockchain()->getCirculativeSuply().toStdString(10));
    BigNumberFloat blockAmount(node->blockchain()->getRecords().toStdString(10));
    BigNumberFloat dataAmountStoredInNetwork(std::to_string(node->dfs()->totalDfsSize()));

    BigNumberFloat result = 100; // Test
    //    BigNumberFloat result = calculateCoins(BigNumberFloat(requestReward.DataStoredSize),
    //    dataAmountStoredInNetwork,
    //         circulativeSupply, blockAmount, coefficient);

    Transaction rewardTx;
    rewardTx.setAmount(result);
    rewardTx.setReceiver(requestReward.Actor);
    rewardTx.setSender(node->actorIndex()->firstId());
    rewardTx.setTypeTx(TypeTx::RewardTransaction);
    rewardTx.setToken(ActorId());
    rewardTx.setPrevBlock(node->blockchain()->getLastRealBlock().getIndex());
    rewardTx.sign(*node->accountController()->mainActor());
    return rewardTx;
}

void DataMiningManager::coinRewardRequest(const BigNumber &blockIndex) {
    if (blockIndex % CoinProductionRate == 0) {
        qDebug() << "Make reward request" << std::stoi(blockIndex.toStdString(10));
        DFSP::StateMessage stateMessage;
        stateMessage.FarmingActor = node->accountController()->farmingIds()[0].toStdString();
        stateMessage.DataAmountStored = node->dfs()->calculateDataAmountStored();
        // if (stateMessage.DataAmountStored > 0)
            // node->network()->send_message(stateMessage, MessageType::DfsState, MessageStatus::Request);
    }
}

void DataMiningManager::interestAccrual() {
    indexBlock++;
    // updateLastIndex();
    if (indexBlock % indexBlockFarming == 0) {
        if(!isRecalculate) {
            calculateFarmingBalanceMainUser();
        }
        BigNumberFloat result = balanceFarming * farmingPercent;
        balanceFarming += result;
        ActorId actorId = node->accountController()->mainActor()->id();
        Transaction tx = node->createFarmingTransaction(actorId, result, TypeTx::FarmingTransaction);
        node->txManager()->addTransaction(tx);
    }
}

BigNumberFloat DataMiningManager::farmingBalance() const {
    return balanceFarming;
}

void DataMiningManager::calculateFarmingBalanceMainUser() {
    auto currentActorId = node->accountController()->currentProfile().current()->id();
    balanceFarming = node->blockchain()->getUserBalance(currentActorId, ActorId(), TypeTx::FarmingTransaction);
    isRecalculate = true;
}

void DataMiningManager::updateLastIndex() {
    DBConnector db(farmingCachePath);
    bool isDbOpen = db.open();
    if (!isFarmingCashEmpty) {
        db.query(fmt::format("UPDATE {} SET blockIndex='{}' WHERE id = '1'",
                             Config::DataStorage::farmingCacheTable, indexBlock.toStdString(10)));
    } else {
        DBRow row;
        row.insert({ "id", "1" });
        row.insert({ "blockIndex", indexBlock.toStdString(10) });
        const bool inserted = db.insert(Config::DataStorage::farmingCacheTable, row);
        if (inserted)
            isFarmingCashEmpty = false;
    }

    db.close();
}
