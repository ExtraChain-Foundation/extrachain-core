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

#include "datastorage/block.h"

Block::Block() {
    this->m_type = Config::DATA_BLOCK_TYPE;

    this->index = BigNumber(-1);
    this->date = QDateTime::currentDateTime().toMSecsSinceEpoch();
    this->data = "";
    this->prevHash = "";
    this->hash = "";
}

Block::Block(const Block &block) {
    this->m_type = block.getType();
    this->index = block.getIndex();
    this->date = block.getDate();
    this->data = block.getData();
    this->prevHash = block.getPrevHash();
    this->hash = block.getHash();
    this->signatures = block.signatures;
}

Block::Block(const std::string &serialized)
    : Block() {
    this->deserialize(serialized);
}

Block::Block(const std::string &data, const Block &prev)
    : Block() {
    if (prev.isEmpty()) {
        // qDebug() << "BLOCK: Construction first block";
        this->index = BigNumber("0");
        this->prevHash = Utils::calcHash("0 index");
    } else {
        // qDebug() << "BLOCK: Construction block. Previous block id - "
        //          << prev->getIndex();
        this->index = prev.getIndex() + 1;
        this->prevHash = prev.getHash();
    }

    this->date = QDateTime::currentDateTime().toMSecsSinceEpoch();

    this->data = data;
}

Block::~Block() {
}

Block Block::operator=(const Block &block) {
    m_type = block.m_type;
    data = block.data;
    index = block.index;
    date = block.date;
    prevHash = block.prevHash;
    hash = block.hash;
    signatures = block.signatures;
    return *this;
}

void Block::calcHash() {
    std::string resultHash = Utils::calcHash(getDataForHash());
    if (!resultHash.empty()) {
        this->hash = resultHash;
    }
}

void Block::setType(const std::string &value) {
    m_type = value;
}

std::string Block::getDataForHash() const {
    std::string idHash = Utils::calcHash(getIndex().toStdString());
    auto list = extractTransactions();
    if (list.empty())
        return idHash;
    std::string txHash = Utils::calcHash(list[0].serialize());
    for (int i = 1; i < list.size(); i++) {
        std::string tmpTxHash = Utils::calcHash(list[i].serialize());
        txHash = Utils::calcHash(txHash + tmpTxHash);
    }
    return idHash + txHash;
}

const std::string &Block::getDataForDigSig() const {
    return hash;
}

void Block::sign(const Actor<KeyPrivate> &actor) {
    calcHash();
    std::string sign = actor.key().sign(getDataForDigSig());
    this->signatures.push_back({ actor.id().toStdString(), sign, true });
}

bool Block::verify(const Actor<KeyPublic> &actor) const {
    bool res = actor.key().verify(getDataForDigSig(), getDigSig());
    return signatures.empty() ? false : res;
}

bool Block::deserialize(const std::string &serialized) {
    if (serialized.empty()) {
        return false;
    } else {
        *this = MessagePack::deserialize<Block>(Utils::bytesDecodeStdString(serialized));
        return true;
    }
}

bool Block::equals(const Block &block) const {
    return hash == block.getHash();
}

BlockCompare Block::compareBlock(const Block &b) const {
    BlockCompare temp;
    temp.approverDiff = BigNumber(getApprover().toStdString()) - BigNumber(b.getApprover().toStdString());
    temp.indexDiff = getIndex() - b.getIndex();
    temp.dataDiff =
        Utils::compare(QByteArray::fromStdString(getData()), QByteArray::fromStdString(b.getData()));
    temp.digitalSigDiff = getDigSig() == b.getDigSig();
    temp.hashDiff = getHash() == b.getHash();
    temp.prevHashDiff = getPrevHash() == b.getPrevHash();
    return temp;
}

void Block::addData(const std::string &data) {
    std::vector<std::string> v = Serialization::deserialize(this->data);
    v.push_back(data);
    this->data = Serialization::serialize(v);
}

void Block::initializeData(const std::string &serializedData) {
    this->data = serializedData;
}

std::vector<Transaction> Block::extractTransactions() const {
    if (m_type != Config::DATA_BLOCK_TYPE)
        return {};

    std::vector<std::string> txsData = Serialization::deserialize(data);

    std::vector<Transaction> transactions;
    for (const std::string &trData : txsData) {
        if (!trData.empty()) {
            Transaction tx(trData);
            if (!tx.isEmpty())
                transactions.push_back(tx);
        }
    }
    return transactions;
}

Transaction Block::getTransactionByHash(std::string hash) const {
    auto txList = extractTransactions();
    for (const auto &i : txList)
        if (i.getHash() == hash)
            return i;
    return Transaction();
}

bool Block::contain(Block &from) const {
    auto ourTx = this->extractTransactions();
    auto fromTx = from.extractTransactions();
    for (const auto &i : fromTx) {
        if (std::find(ourTx.begin(), ourTx.end(), i) == ourTx.end()) {
            return false;
        }
    }
    return true;
}

std::string Block::serialize() const {
    return Utils::bytesEncodeStdString(MessagePack::serialize(*this));
}

QString Block::toString() const {
    return QString::fromStdString(this->serialize());
}

bool Block::isEmpty() const {
    return this->getHash().empty() && this->getDigSig().empty() && this->getPrevHash().empty();
}

std::string Block::getType() const {
    return m_type;
}

std::string Block::getDigSig() const {
    return signatures.empty() ? "" : this->signatures.begin()->sign;
}

QByteArrayList Block::getListSignatures() const {
    QByteArrayList res;

    for (auto const &signature : signatures) {
        res << QByteArray::fromStdString(signature.actorId) << QByteArray::fromStdString(signature.sign)
            << (signature.isApprove ? "1" : "0");
    }

    return res;
}

void Block::addSignature(const QByteArray &id, const QByteArray &sign, const bool &isApprover) {
    this->signatures.push_back({ id.toStdString(), sign.toStdString(), isApprover });
}
// void Block::setType(QByteArray type)
//{
//    this->type = type;
//}

void Block::setPrevHash(const std::string &value) {
    prevHash = value;
}

ActorId Block::getApprover() const {
    if (signatures.empty()) {
        return ActorId();
    } else {
        for (int i = signatures.size() - 1; i >= 0; i--) {
            if (signatures[i].isApprove)
                return signatures[i].actorId;
        }
    }

    return ActorId();
}

BigNumber Block::getIndex() const {
    return index;
}

std::string Block::getData() const {
    return data;
}

std::string Block::getHash() const {
    return hash;
}

std::string Block::getPrevHash() const {
    return prevHash;
}

bool Block::operator<(const Block &other) {
    if (this->index < other.getIndex()) {
        return true;
    } else if (this->data < other.getData()) {
        return true;
    }
    return false;
}

bool Block::isBlock(const QByteArray &data) {
    return (data.contains(Config::DATA_BLOCK_TYPE) || data.contains(Config::DUMMY_BLOCK_TYPE));
}

bool Block::isApprover(const ActorId &actorId) const {
    return actorId == getApprover();
}

void Block::initFields(QList<QByteArray> &list) {
    m_type = list.takeFirst();
    index = BigNumber(list.takeFirst().toStdString());
    date = list.takeFirst().toLongLong();
    data = list.takeFirst();
    prevHash = list.takeFirst();
    hash = list.takeFirst();
    QByteArray signs = list.takeFirst();
    std::vector<std::string> lists = Serialization::deserialize(signs.toStdString());
    for (const auto &tmp : lists) {
        std::vector<std::string> tmps = Serialization::deserialize(tmp);
        if (tmps.size() == 3) {
            signatures.push_back({ tmps.at(0), tmps.at(1), bool(std::stoi(tmps.at(2))) });
        }
    }
}

long long Block::getDate() const {
    return date;
}

void Block::setDate(long long value) {
    date = value;
}
