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

#include "datastorage/transaction.h"

Transaction::Transaction() {
    this->amount = BigNumberFloat(0);
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->data = std::string();
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->digSig = std::string();
    this->typeTx = TypeTx::Transaction;
    calcHash();
}

Transaction::Transaction(const std::string &serialized) {
    if (serialized.empty()) {
        qDebug() << "Incorrect TX";
        return;
    }

    deserialize(serialized);
    calcHash();
}

Transaction::Transaction(const ActorId &sender, const ActorId &receiver, const BigNumberFloat &amount) {
    this->sender = sender;
    this->receiver = receiver;
    this->amount = amount;
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->data = std::string();
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->digSig = std::string();
    this->typeTx = TypeTx::Transaction;
    calcHash();
}

Transaction::Transaction(const ActorId &sender, const ActorId &receiver, const BigNumberFloat &amount,
                         const std::string &data)
    : Transaction(sender, receiver, amount) {
    this->data = data;
    this->typeTx = TypeTx::Transaction;
    calcHash();
}

Transaction::Transaction(const Transaction &other) {
    this->sender = other.sender;
    this->receiver = other.receiver;
    this->amount = other.amount;
    this->date = other.date;
    this->data = other.data;
    this->token = other.token;
    this->prevBlock = other.prevBlock;
    this->gas = other.gas;
    this->hop = other.hop;
    this->hash = other.hash;
    this->approver = other.approver;
    this->digSig = other.digSig;
    this->producer = other.producer;
    this->typeTx = other.typeTx;
    calcHash();
}

void Transaction::setReceiver(const ActorId &value) {
    receiver = value;
}

bool Transaction::isRewardTransaction() const {
    return typeTx == TypeTx::RewardTransaction;
}

void Transaction::setProducer(const ActorId &value) {
    producer = value;
}

void Transaction::setDigSig(const std::string &value) {
    digSig = value;
}

void Transaction::setApprover(const ActorId &value) {
    approver = value;
}

void Transaction::setHash(const std::string &value) {
    hash = value;
}

void Transaction::setSender(const ActorId &value) {
    sender = value;
}

ActorId Transaction::getProducer() const {
    return producer;
}

void Transaction::setAmount(const BigNumberFloat &value) {
    amount = value;
}

void Transaction::setData(const std::string &value) {
    data = value;
}

void Transaction::setToken(const ActorId &value) {
    token = value;
}

long long Transaction::getDate() const {
    return date;
}

void Transaction::setDate(long long value) {
    date = value;
}

void Transaction::calcHash() {
    std::string resultHash = Utils::calcHash(getDataForHash());
    if (!resultHash.empty()) {
        this->hash = resultHash;
    }
}

TypeTx Transaction::getTypeTx() const {
    return typeTx;
}

void Transaction::setTypeTx(TypeTx newTypeTx) {
    typeTx = newTypeTx;
}

std::string Transaction::getDataForHash() const {
    return (sender.toStdString() + receiver.toStdString() + amount.toStdString() + std::to_string(date) + data
            + token.toStdString() + prevBlock.toStdString() + std::to_string(gas) + approver.toStdString()
            + producer.toStdString());
}

std::string Transaction::getDataForDigSig() const {
    return getDataForHash() + hash;
}

void Transaction::sign(const Actor<KeyPrivate> &actor) {
    this->approver = actor.id();
    calcHash();
    this->digSig = actor.key().sign(getDataForDigSig());
}

bool Transaction::verify(const Actor<KeyPublic> &actor) const {
    return digSig.empty() ? false : actor.key().verify(getDataForDigSig(), getDigSig());
}

int Transaction::getHop() const {
    return hop;
}

void Transaction::setPrevBlock(const BigNumber &value) {
    this->prevBlock = value;

    calcHash();
}

void Transaction::setGas(int gas) {
    this->gas = gas;

    calcHash();
}

void Transaction::setHop(int hop) {
    this->hop = hop;

    calcHash();
}

void Transaction::decrementHop() {
    this->hop--;
    calcHash();
}

void Transaction::clear() {
    this->sender = "0";
    this->receiver = "0";
    this->amount = BigNumberFloat(0);
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->data = std::string();
    this->token = "0";
    this->prevBlock = BigNumber(0);
    this->gas = 0;
    this->hop = 0;
    this->hash = "";
    this->approver = "0";
    this->digSig = std::string();
    this->producer = "0";
    calcHash();
}

int Transaction::getGas() const {
    return this->gas;
}

ActorId Transaction::getSender() const {
    return this->sender;
}

ActorId Transaction::getReceiver() const {
    return this->receiver;
}

BigNumberFloat Transaction::getAmount() const {
    return this->amount;
}

BigNumber Transaction::getPrevBlock() const {
    return this->prevBlock;
}

std::string Transaction::getHash() const {
    return this->hash;
}

ActorId Transaction::getToken() const {
    return this->token;
}

ActorId Transaction::getApprover() const {
    return this->approver;
}

std::string Transaction::getData() const {
    return this->data;
}

std::string Transaction::getDigSig() const {
    return this->digSig;
}

bool Transaction::isEmpty() const {
    return sender.isEmpty() && receiver.isEmpty() && amount.isEmpty() && data.empty() && prevBlock.isEmpty()
        && approver.isEmpty() && hash.empty();
}

bool Transaction::deserialize(const std::string &serialized) {
    *this = MessagePack::deserialize<Transaction>(serialized);
    return true;
}

bool Transaction::operator==(const Transaction &transaction) const {
    if (this->sender != transaction.getSender())
        return false;
    if (this->receiver != transaction.getReceiver())
        return false;
    if (this->amount != transaction.getAmount())
        return false;
    if (this->date != transaction.getDate())
        return false;
    if (this->data != transaction.getData())
        return false;
    if (this->token != transaction.getToken())
        return false;
    if (this->gas != transaction.getGas())
        return false;
    if (this->hop != transaction.getHop())
        return false;
    //    if (this->hash != transaction.getHash())
    //        return false;
    //    if (this->approver != transaction.getApprover())
    //        return false;
    if (this->prevBlock != transaction.getPrevBlock())
        return false;
    //    if (this->digSig != transaction.getDigSig())
    //        return false;
    return true;
}

bool Transaction::operator!=(const Transaction &transaction) const {
    return !(*this == transaction);
}

void Transaction::operator=(const Transaction &other) {
    this->sender = other.sender;
    this->receiver = other.receiver;
    this->amount = other.amount;
    this->date = other.date;
    this->data = other.data;
    this->token = other.token;
    this->prevBlock = other.prevBlock;
    this->gas = other.gas;
    this->hop = other.hop;
    this->hash = other.hash;
    this->approver = other.approver;
    this->digSig = other.digSig;
    this->producer = other.producer;
    this->typeTx = other.typeTx;
}

std::string Transaction::serialize() const {
    return MessagePack::serialize(*this);
}

QString Transaction::toString() const {
    return "sender:" + sender.toByteArray() + ", receiver:" + receiver.toByteArray()
        + ", amount:" + amount.toByteArray() + ", date:" + QDateTime::fromMSecsSinceEpoch(date).toString()
        + ", data:" + QString::fromStdString(data) + ", token:" + token.toByteArray()
        + ", prevBlock:" + prevBlock.toByteArray() + ", gas:" + QString::number(gas)
        + ", hop:" + QString::number(hop) + ", hash:" + QString::fromStdString(hash)
        + ", approver:" + approver.toByteArray() + ", digitalSignature:" + QString::fromStdString(digSig);
}

BigNumberFloat Transaction::visibleToAmount(std::string amount) {
    if (amount.empty())
        return 0;

    amount += amount.find(".") == -1 ? "." : "";
    std::vector<std::string> amountList;
    boost::algorithm::split(amountList, amount, boost::algorithm::is_any_of("."));
    int secondLength = amountList[1].length();
    amount.append(18 - secondLength, '0');
    amount = std::regex_replace(amount, std::regex("."), "");

    return BigNumberFloat(amount, 10);
}

QString Transaction::amountToVisible(const BigNumberFloat &number) {
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

BigNumberFloat Transaction::amountNormalizeMul(const BigNumberFloat &number) {
    QByteArray n = number.toByteArray(10);
    if (n.length() < 36)
        return number;
    return BigNumberFloat(n.chopped(18).toStdString(), 10);
}

BigNumberFloat Transaction::amountMul(const BigNumberFloat &number1, const BigNumberFloat &number2) {
    QByteArray one = Transaction::amountToVisible(number1).toLatin1();
    QByteArray two = Transaction::amountToVisible(number1).toLatin1();
    int index1 = one.indexOf(".");
    int index2 = two.indexOf(".");
    int div1 = one.size() - index1 - 1;
    int div2 = two.size() - index2 - 1;
    BigNumberFloat returned1 = index1 == -1 ? 1 : BigNumberFloat(10).pow(div1);
    BigNumberFloat returned2 = index2 == -1 ? 1 : BigNumberFloat(10).pow(div2);

    BigNumberFloat number = (number1 * returned1) * (number2 * returned2);

    return amountNormalizeMul(number) / returned1 / returned2;
}

BigNumberFloat Transaction::amountDiv(const BigNumberFloat &number1, const BigNumberFloat &number2) {
    QByteArray two = Transaction::amountToVisible(number2).toLatin1();
    int index = two.indexOf(".");
    int div = two.size() - index - 1;
    QByteArray newTwoByte = two.remove(index, 1);

    BigNumberFloat returned = index == -1 ? 1 : BigNumberFloat(10).pow(div);
    auto second = BigNumberFloat(newTwoByte.toStdString(), 10);
    if (second == 0)
        return 0;

    return number1 * returned / second;
}

BigNumberFloat Transaction::amountPercent(BigNumberFloat number, uint percent) {
    if (percent > 100)
        percent = 100;
    return number * percent / 100;
}
