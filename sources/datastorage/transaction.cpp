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
    this->sender = ActorId();
    this->receiver = ActorId();
    this->token = ActorId();
    this->approver = ActorId();
    this->producer = ActorId();
    this->amount = BigNumberFloat(0);
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->data = std::string();
    this->prevBlock = BigNumber(0);
    this->hash = "";
    this->signature = std::string();
    this->typeTx = TypeTx::Transaction;
    calcHash();
}

Transaction::Transaction(
    const ActorId &sender,
    const ActorId &receiver,
    const BigNumberFloat &amount,
    const ActorId &token,
    const std::string &data) {
    this->sender = sender;
    this->receiver = receiver;
    this->amount = amount;
    this->date = QDateTime::currentMSecsSinceEpoch();
    this->data = data;
    this->prevBlock = BigNumber(0);
    this->hash = "";
    this->signature = std::string();
    this->typeTx = TypeTx::Transaction;
    this->token = token;
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
    this->hash = other.hash;
    this->approver = other.approver;
    this->signature = other.signature;
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

bool Transaction::isFarmingTransaction() const {
    return typeTx == TypeTx::FarmingTransaction;
}

bool Transaction::isLockedFarmingTransaction() const {
    return typeTx == TypeTx::FarmingLockedTransaction;
}

void Transaction::setProducer(const ActorId &value) {
    producer = value;
}

void Transaction::setSignature(const std::string &value) {
    signature = value;
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
    auto hashData = sender.toStdString() + receiver.toStdString() + amount.toStdString(NumeralBase::Hex)
                    + data + std::to_string(date) + token.toStdString() + prevBlock.toStdString()
                    + approver.toStdString() + producer.toStdString();

    std::string resultHash = Utils::calcHash(hashData);
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

void Transaction::sign(const std::shared_ptr<Actor<KeyPrivate>> actor) {
    this->approver = actor->id();
    calcHash();
    this->signature = actor->key().sign(hash);
}

bool Transaction::verify(const Actor<KeyPublic> &actor) const {
    return signature.empty() ? false : actor.key().verify(hash, getSignature());
}

void Transaction::setPrevBlock(const BigNumber &value) {
    this->prevBlock = value;

    calcHash();
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

std::string Transaction::getAmountDec() const {
    return this->amount.toStdString(NumeralBase::Dec);
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

std::string Transaction::getSignature() const {
    return this->signature;
}

bool Transaction::isEmpty() const {
    return sender.isZero() && receiver.isZero() && amount.isEmpty() && data.empty() && prevBlock.isEmpty()
           && approver.isZero() && hash.empty();
}

bool Transaction::isBurn() const {
    return sender.isZero() && amount.isEmpty() && data.empty() && prevBlock.isEmpty() && approver.isZero()
           && hash.empty();
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
    //    if (this->hash != transaction.getHash())
    //        return false;
    //    if (this->approver != transaction.getApprover())
    //        return false;
    if (this->prevBlock != transaction.getPrevBlock())
        return false;
    //    if (this->signature != transaction.getSignature())
    //        return false;
    return true;
}

void Transaction::operator=(const Transaction &other) {
    this->sender = other.sender;
    this->receiver = other.receiver;
    this->amount = other.amount;
    this->date = other.date;
    this->data = other.data;
    this->token = other.token;
    this->prevBlock = other.prevBlock;
    this->hash = other.hash;
    this->approver = other.approver;
    this->signature = other.signature;
    this->producer = other.producer;
    this->typeTx = other.typeTx;
}

std::string Transaction::toStdString() const {
    return toString().toStdString();
}

QString Transaction::toString() const {
    auto hashQt = QString::fromStdString(hash);
    auto typeStr = QString::fromStdString(Utils::enumFullName(typeTx));
    return "Transaction { type: " + typeStr + ", sender: " + sender.toByteArray() + ", receiver: " + receiver.toByteArray()
           + ", amount: " + amount.toByteArray(NumeralBase::Dec) + ", date: "
           + QDateTime::fromMSecsSinceEpoch(date).toString() + ", data: '" + QString::fromStdString(data)
           + "', token: " + token.toByteArray() + ", prevBlock: " + prevBlock.toByteArray() + ", hash: '"
           + hashQt.left(5) + ".." + hashQt.right(5) + "', approver: " + approver.toByteArray()
           + ", digitalSignature: '" + QString::fromStdString(signature) + "' }";
}

QDebug operator<<(QDebug debug, const Transaction &tx) {
    QDebugStateSaver saver(debug);
    debug.nospace().noquote() << tx.toString();
    return debug;
}
