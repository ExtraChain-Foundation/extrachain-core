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

RewardTransaction::RewardTransaction() {
    this->amount = BigNumber(0);
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->rewardData = TransactionRewardData();
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->digSig = QByteArray();
    calcHash();
}

RewardTransaction::RewardTransaction(const QByteArray &serialized) {
    if (serialized.isEmpty()) {
        qDebug() << "Incorrect TX";
        return;
    }

    deserialize(serialized);
    calcHash();
}

RewardTransaction::RewardTransaction(const ActorId &senderReceiver) {
    this->senderReceiver = senderReceiver;
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->rewardData = TransactionRewardData();
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->digSig = QByteArray();
    calcAmount();
    calcHash();
}

RewardTransaction::RewardTransaction(const ActorId &senderReceiver, const TransactionRewardData &rewardData)
    : RewardTransaction(senderReceiver)
{
    this->rewardData = rewardData;
    calcAmount();
    calcHash();
}

RewardTransaction::RewardTransaction(const ActorId &senderReceiver, const QByteArray &data,
                                     const TransactionRewardData &rewardData)
    : RewardTransaction(senderReceiver) {
    this->rewardData = rewardData;
    calcAmount();
    calcHash();
}

RewardTransaction::RewardTransaction(const RewardTransaction &other) {
    this->senderReceiver = other.senderReceiver;
    this->date = other.date;
    this->rewardData = other.rewardData;
    this->token = other.token;
    this->prevBlock = other.prevBlock;
    this->gas = other.gas;
    this->hop = other.hop;
    this->hash = other.hash;
    this->approver = other.approver;
    this->digSig = other.digSig;
    this->producer = other.producer;
    calcAmount();
    calcHash();
}

void RewardTransaction::setProducer(const ActorId &value) {
    producer = value;
}

void RewardTransaction::setDigSig(const std::string &value) {
    digSig = value;
}

void RewardTransaction::setApprover(const ActorId &value) {
    approver = value;
}

void RewardTransaction::setHash(const std::string &value) {
    hash = value;
}

void RewardTransaction::setSenderReceiver(const ActorId &value) {
    senderReceiver = value;
}

void RewardTransaction::insertAdditionalData(AdditionalData &additionalData)
{
    rewardData.additionalData.push_back(additionalData);
}

void RewardTransaction::calcAmount() {
    int result = 0;
    for (const auto &data : rewardData.additionalData) {
        result += data.calcReward();
    }
    this->amount = BigNumber(result);
}

ActorId RewardTransaction::getProducer() const {
    return producer;
}

void RewardTransaction::setAmount(const BigNumber &value) {
    amount = value;
}

void RewardTransaction::setRewardData(const TransactionRewardData &transactionRewardData) {
    rewardData = transactionRewardData;
    calcAmount();
}

void RewardTransaction::setToken(const ActorId &value) {
    token = value;
}

long long RewardTransaction::getDate() const {
    return date;
}

void RewardTransaction::setDate(long long value) {
    date = value;
}

void RewardTransaction::calcHash() {
    QByteArray resultHash = Utils::calcHash(getDataForHash());
    if (!resultHash.isEmpty()) {
        this->hash = resultHash;
    }
}

QByteArray RewardTransaction::getDataForHash() const {
    return (senderReceiver.toByteArray() + amount.toByteArray() + QByteArray::number(date)
            + token.toByteArray() + prevBlock.toByteArray() + QByteArray::number(gas) + approver.toByteArray()
            + producer.toByteArray());
}

QByteArray RewardTransaction::getDataForDigSig() const {
    return getDataForHash() + QByteArray::fromStdString(hash);
}

void RewardTransaction::sign(const Actor<KeyPrivate> &actor) {
    this->approver = actor.id();
    calcHash();
    this->digSig = actor.key().sign(getDataForDigSig().toStdString());
}

bool RewardTransaction::verify(const Actor<KeyPublic> &actor) const {
    return digSig.empty() ? false
                          : actor.key().verify(getDataForDigSig().toStdString(), getDigSig().toStdString());
}

int RewardTransaction::getHop() const {
    return hop;
}

void RewardTransaction::setPrevBlock(const BigNumber &value) {
    this->prevBlock = value;
    calcHash();
}

void RewardTransaction::setGas(int gas) {
    this->gas = gas;
    calcHash();
}

void RewardTransaction::setHop(int hop) {
    this->hop = hop;

    calcHash();
}

void RewardTransaction::decrementHop() {
    this->hop--;
    calcHash();
}

void RewardTransaction::clear() {
    this->senderReceiver = "0";
    this->amount = BigNumber(0);
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->rewardData = TransactionRewardData();
    this->token = "0";
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->approver = "0";
    this->digSig = QByteArray();
    this->producer = "0";
    calcHash();
}

int RewardTransaction::getGas() const {
    return this->gas;
}

ActorId RewardTransaction::getSenderReceiver() const {
    return this->senderReceiver;
}

BigNumber RewardTransaction::getAmount() const {
    return this->amount;
}

BigNumber RewardTransaction::getPrevBlock() const {
    return this->prevBlock;
}

QByteArray RewardTransaction::getHash() const {
    return QByteArray::fromStdString(this->hash);
}

ActorId RewardTransaction::getToken() const {
    return this->token;
}

ActorId RewardTransaction::getApprover() const {
    return this->approver;
}

TransactionRewardData RewardTransaction::getRewardData() const {
    return rewardData;
}

QByteArray RewardTransaction::getDigSig() const {
    return QByteArray::fromStdString(this->digSig);
}

bool RewardTransaction::isEmpty() const {
    return senderReceiver.isEmpty() && amount.isEmpty() && prevBlock.isEmpty() && approver.isEmpty()
        && hash.empty() && rewardData.isEmpty();
}

bool RewardTransaction::deserialize(const QByteArray &serialized) {
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
//        if (this->rewardData != transaction.getRewardData())
    //        return false;
    return true;
}

bool RewardTransaction::operator!=(const RewardTransaction &transaction) const {
    return !(*this == transaction);
}

void RewardTransaction::operator=(const RewardTransaction &other) {
    this->senderReceiver = other.senderReceiver;
    this->amount = other.amount;
    this->date = other.date;
    this->rewardData = other.rewardData;
    this->token = other.token;
    this->prevBlock = other.prevBlock;
    this->gas = other.gas;
    this->hop = other.hop;
    this->hash = other.hash;
    this->approver = other.approver;
    this->digSig = other.digSig;
    this->producer = other.producer;
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

BigNumber RewardTransaction::visibleToAmount(QByteArray amount) {
    if (amount.isEmpty())
        return 0;

    amount += amount.indexOf(".") == -1 ? "." : "";
    QByteArrayList amountList = amount.split('.');
    int secondLength = amountList[1].length();

    amount += QString("0").repeated(18 - secondLength).toLatin1();
    amount.replace(".", "");

    return BigNumber(amount.toStdString(), 10);
}

QString RewardTransaction::amountToVisible(const BigNumber &number) {
    if (number == 0)
        return "0";

    QByteArray numberArr = number.toByteArray(10);
    bool minus = false;

    if (numberArr[0] == '-') {
        numberArr = numberArr.remove(0, 1);
        minus = true;
    }

    QString second = numberArr.right(18); // TODO
    second = QString("0").repeated(18 - second.length()).toLatin1() + second;
    second = second.remove(QRegularExpression("[0]*$"));
    QByteArray first = numberArr.left(numberArr.length() - 18);

    QByteArray numberDec = (first.isEmpty() ? QByteArray("0") : first)
        + (second.toLatin1() == QByteArray("0") || second.isEmpty() ? QByteArray("")
                                                                    : QByteArray(".") + second.toLatin1());

    return (minus ? "-" : "") + numberDec;
}

BigNumber RewardTransaction::amountNormalizeMul(const BigNumber &number) {
    QByteArray n = number.toByteArray(10);
    if (n.length() < 36)
        return number;
    return BigNumber(n.chopped(18).toStdString(), 10);
}

BigNumber RewardTransaction::amountMul(const BigNumber &number1, const BigNumber &number2) {
    QByteArray one = RewardTransaction::amountToVisible(number1).toLatin1();
    QByteArray two = RewardTransaction::amountToVisible(number1).toLatin1();
    int index1 = one.indexOf(".");
    int index2 = two.indexOf(".");
    int div1 = one.size() - index1 - 1;
    int div2 = two.size() - index2 - 1;
    BigNumber returned1 = index1 == -1 ? 1 : BigNumber(10).pow(div1);
    BigNumber returned2 = index2 == -1 ? 1 : BigNumber(10).pow(div2);

    BigNumber number = (number1 * returned1) * (number2 * returned2);

    return amountNormalizeMul(number) / returned1 / returned2;
}

BigNumber RewardTransaction::amountDiv(const BigNumber &number1, const BigNumber &number2) {
    QByteArray two = RewardTransaction::amountToVisible(number2).toLatin1();
    int index = two.indexOf(".");
    int div = two.size() - index - 1;
    QByteArray newTwoByte = two.remove(index, 1);

    BigNumber returned = index == -1 ? 1 : BigNumber(10).pow(div);
    auto second = BigNumber(newTwoByte.toStdString(), 10);
    if (second == 0)
        return 0;

    return number1 * returned / second;
}

BigNumber RewardTransaction::amountPercent(BigNumber number, uint percent) {
    if (percent > 100)
        percent = 100;
    return number * percent / 100;
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
