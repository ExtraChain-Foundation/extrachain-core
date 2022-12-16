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

#include "datastorage/reward_transaction.h"

RewardTransaction::RewardTransaction()
    : Transaction() {
    this->rewardData = TransactionRewardData();
    typeTx = TypeTransaction::TypeTx::RewardTransaction;
    calcHash();
}

RewardTransaction::RewardTransaction(const std::string &serialized) {
    if (serialized.empty()) {
        qDebug() << "Incorrect TX";
        return;
    }

    deserialize(serialized);
    calcHash();
    calcAmount();
}

RewardTransaction::RewardTransaction(const ActorId &senderReceiver)
    : Transaction() {
    this->senderReceiver = senderReceiver;
    typeTx = TypeTransaction::TypeTx::RewardTransaction;
    calcAmount();
    calcHash();
}

RewardTransaction::RewardTransaction(const ActorId &senderReceiver, const TransactionRewardData &rewardData)
    : RewardTransaction(senderReceiver) {
    this->rewardData = rewardData;
    typeTx = TypeTransaction::TypeTx::RewardTransaction;
    calcAmount();
    calcHash();
}

RewardTransaction::RewardTransaction(const ActorId &senderReceiver, const std::string &data,
                                     const TransactionRewardData &rewardData)
    : RewardTransaction(senderReceiver) {
    this->rewardData = rewardData;
    typeTx = TypeTransaction::TypeTx::RewardTransaction;
    calcAmount();
    calcHash();
}

RewardTransaction::RewardTransaction(const RewardTransaction &other)
    : Transaction(other) {
    this->senderReceiver = other.senderReceiver;
    this->rewardData = other.rewardData;
    typeTx = TypeTransaction::TypeTx::RewardTransaction;
    calcAmount();
    calcHash();
}

RewardTransaction::RewardTransaction(const Transaction &transaction) {
    //    const auto serialized = transaction.getData().remove(0, TypeTransaction::rewardTx.length());
    qDebug() << transaction.getData().size() << transaction.getData().c_str();
    deserialize(transaction.getData());
    typeTx = TypeTransaction::TypeTx::RewardTransaction;
    calcHash();
    calcAmount();
}

void RewardTransaction::setSenderReceiver(const ActorId &value) {
    senderReceiver = value;
}

void RewardTransaction::insertAdditionalData(AdditionalData &additionalData) {
    rewardData.additionalData.push_back(additionalData);
}

Transaction RewardTransaction::convertToTransaction() {
    Transaction tx = *this;
    tx.setTypeTx(TypeTransaction::TypeTx::RewardTransaction);
    tx.setData(serialize());
    tx.setSender(getSenderReceiver());
    tx.setReceiver(getSenderReceiver());
    qDebug() << "txData:" << tx.getData().length() << tx.getData().c_str() << tx.getTypeTx();
    return tx;
}

void RewardTransaction::setAmountReward(const BigNumber &value) {
    amountReward = value;
}

BigNumber RewardTransaction::getAmountReward() const {
    return amountReward;
}

void RewardTransaction::calcAmount() {
    int result = 0;
    for (const auto &data : rewardData.additionalData) {
        result += data.calcReward();
    }
    this->amount = BigNumber(result);
}

void RewardTransaction::setRewardData(const TransactionRewardData &transactionRewardData) {
    rewardData = transactionRewardData;
    calcAmount();
}

void RewardTransaction::clear() {
    Transaction::clear();
    senderReceiver = "0";
    rewardData = TransactionRewardData();

    calcHash();
}

bool RewardTransaction::isEmpty() const {
    return senderReceiver.isEmpty() && amount.isEmpty() && prevBlock.isEmpty() && approver.isEmpty()
        && hash.empty() && rewardData.isEmpty();
}

bool RewardTransaction::deserialize(const std::string &serialized) {
    *this = MessagePack::deserialize<RewardTransaction>(serialized);
    return true;
}

bool RewardTransaction::operator==(const RewardTransaction &transaction) const {
    if (this->senderReceiver != transaction.getSenderReceiver())
        return false;
    if (this->amount != transaction.getAmount())
        return false;
    if (this->date != transaction.getDate())
        return false;
    if (this->token != transaction.getToken())
        return false;
    if (this->gas != transaction.getGas())
        return false;
    if (this->hop != transaction.getHop())
        return false;
    if (this->prevBlock != transaction.getPrevBlock())
        return false;

    return true;
}

bool RewardTransaction::operator!=(const RewardTransaction &transaction) const {
    return !(*this == transaction);
}

void RewardTransaction::operator=(const RewardTransaction &other) {
    Transaction::operator=(other);
    this->senderReceiver = other.senderReceiver;
    this->rewardData = other.rewardData;
}

std::string RewardTransaction::serialize() const {
    return MessagePack::serialize(*this);
}

QString RewardTransaction::toString() const {
    return "sender:" + senderReceiver.toByteArray() + +", amount:" + amount.toByteArray()
        + ", date:" + QDateTime::fromMSecsSinceEpoch(date).toString() + ", token:" + token.toByteArray()
        + ", prevBlock:" + prevBlock.toByteArray() + ", gas:" + QString::number(gas)
        + ", hop:" + QString::number(hop) + ", hash:" + QString::fromStdString(hash)
        + ", approver:" + approver.toByteArray() + ", digitalSignature:" + QString::fromStdString(digSig);
}

int AdditionalData::calcReward() const {
    int totalSize = 0;
    for (const auto &verifiedFragment : verifiedFragments) {
        totalSize += verifiedFragment.Size;
    }
    return totalSize;
}

bool TransactionRewardData::isEmpty() const {
    return recieveDataList.empty() && additionalData.empty();
}
