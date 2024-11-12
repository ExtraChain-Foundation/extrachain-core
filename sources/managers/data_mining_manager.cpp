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
#include "blockchain/blockchain.h"
#include "dfs/dfs_controller.h"
#include "managers/transaction_manager.h"
#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

DataMiningManager::DataMiningManager(ExtraChainNode *node, QObject *parent)
    : QObject(parent) {
    this->node = node;
}

BigNumberFloat DataMiningManager::calculateCoins(
    BigNumberFloat dataAmountStored,
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
        coinProducedForNode = calculateCoins(
            dataAmountStored,
            dataAmountTotalStoredInNetwork,
            circulativeSupply,
            blockAmount,
            coefficient);
    }
    return coinProducedForNode;
}

/*
Transaction DataMiningManager::makeRewardTx(const MessageBody &mb) {
    DFSP::StateMessage state = MessagePack::deserialize<DFSP::StateMessage>(mb.data);
    BigNumberFloat circulativeSupply = node->blockchain()->getCirculativeSuply();
    BigNumberFloat blockAmount = node->blockchain()->getRecords();
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
    rewardTx.sign(node->accountController()->mainActor());
    qDebug() << rewardTx.getTypeTx();
    return rewardTx;
}

Transaction
DataMiningManager::makeRewardTx(const DFS::Reward::RequestReward &requestReward, const double coefficient) {
    BigNumberFloat circulativeSupply = node->blockchain()->getCirculativeSuply();
    BigNumberFloat blockAmount = node->blockchain()->getRecords();
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
    rewardTx.sign(node->accountController()->mainActor());
    return rewardTx;
}

void DataMiningManager::coinRewardRequest(const BigNumber &blockIndex) {
    if (blockIndex % CoinProductionRate == 0) {
        qDebug() << "Make reward request:" << std::stoi(blockIndex.toStdString(NumeralBase::Dec));
        DFSP::StateMessage stateMessage;
        stateMessage.FarmingActor = node->accountController()->farmingIds()[0].toStdString();
        stateMessage.DataAmountStored = node->dfs()->calculateDataAmountStored();
        if (stateMessage.DataAmountStored > 0)
            node->network()->send_message(stateMessage, MessageType::DfsState, MessageStatus::Request);
    }
}
*/

void DataMiningManager::requestCoinReward() {
    const std::shared_ptr<Actor<KeyPrivate>> actor = node->accountController()->mainActor();
    auto totalBytes                                = node->network()->getCalculateTraffic()->totalBytes();

    auto requestReward = Dfs::Reward::RequestReward { .Actor              = actor->id(),
                                                      .DataStoredSize     = node->dfs()->sizeTaken(),
                                                      .TypeFunctioningObj = Dfs::Reward::Base,
                                                      .RewardAmount       = calculateRewardAmount(),
                                                      .BytesSent          = totalBytes.first,
                                                      .BytesReceived      = totalBytes.second,
                                                      .BlocksStored = node->blockchain()->getBlocksStored() };

    node->network()->send_message(requestReward, MessageType::BlockchainCoinReward, MessageStatus::Request);
}

BigNumberFloat DataMiningManager::calculateRewardAmount() const {
    // (dataStoredSize/dfsSize + bytesReceived/BytesSent)+(blocksStoredSize/blockchainSize) * k (k=100)
    const auto &totalBytes = node->network()->getCalculateTraffic()->totalBytes();

    if (totalBytes.first == 0 || node->dfs()->totalDfsSize() == 0) {
        // qDebug() << "[Blockchain] Cannot calculate  due to division by zero. TotalBytes, total dfs:"
        //          << totalBytes.first << node->dfs()->totalDfsSize();
        return BigNumberFloat(0);
    }

    auto lastBlock = node->blockchain()->getLastBlock();
    if (!lastBlock.has_value())
        return BigNumberFloat(0);
    if (lastBlock->isEmpty())
        return BigNumberFloat(0);
    auto lastIndex = lastBlock->getIndex();
    if (lastIndex == BigNumber(0))
        return BigNumberFloat(0);

    auto sizeTaken        = BigNumberFloat(node->dfs()->sizeTaken());
    auto totalDfsSize     = BigNumberFloat(node->dfs()->totalDfsSize());
    auto totalBytesFirst  = BigNumberFloat(totalBytes.first);
    auto totalBytesSecond = BigNumberFloat(totalBytes.second);
    auto blocksStored     = BigNumberFloat(node->blockchain()->getBlocksStored());

    return sizeTaken / totalDfsSize + totalBytesSecond / totalBytesFirst
           + (blocksStored / BigNumberFloat(lastIndex) * 100);
}

BigNumberFloat
DataMiningManager::calculateRewardAmount(const Dfs::Reward::RequestReward &requestReward) const {
    if (requestReward.BytesSent == 0 || node->dfs()->totalDfsSize() == 0) {
        // qDebug() << "[Blockchain] Cannot calculate reward due to division by zero. BytesSent, total dfs:"
        //          << requestReward.BytesSent << node->dfs()->totalDfsSize();
        return BigNumberFloat(0);
    }

    auto lastBlock = node->blockchain()->getLastBlock();
    if (!lastBlock.has_value())
        return BigNumberFloat(0);
    if (lastBlock->isEmpty())
        return BigNumberFloat(0);
    auto lastIndex = lastBlock->getIndex();
    if (lastIndex == 0)
        return BigNumberFloat(0);

    return (
        BigNumberFloat { requestReward.DataStoredSize } / node->dfs()->totalDfsSize()
        + BigNumberFloat { requestReward.BytesReceived } / requestReward.BytesSent
        + (BigNumberFloat { requestReward.BlocksStored } / BigNumberFloat(lastIndex) * 100));
}

void DataMiningManager::sendCoinsReward(const Dfs::Reward::RequestReward &requestReward) {
    if ((calculateRewardAmount(requestReward) - requestReward.RewardAmount) <= 100) {
        Transaction transaction;
        transaction.setSender(ActorId());
        transaction.setReceiver(requestReward.Actor);
        transaction.setAmount(requestReward.RewardAmount);
        transaction.setDate(QDateTime::currentMSecsSinceEpoch());
        transaction.setType(TransactionType::Reward);

        if (transaction.amount() <= 0)
            return;

        node->sendTransaction(transaction, node->accountController()->mainActor());
    }
}

// void DataMiningManager::calculateFarmingBalanceMainUser() {
//     auto currentActorId = node->accountController()->currentProfile().current()->id();
//     balanceFarming = node->blockchain()->getUserBalance(currentActorId, ActorId(),
//     TransactionType::FarmingTransaction); isRecalculate = true;
// }
