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
#include "datastorage/block_variant.h"

#include "sha3.h"

Block::Block() {
    this->m_type = BlockType::Data;
    this->m_index = BigNumber(-1);
    this->m_date = QDateTime::currentDateTime().toMSecsSinceEpoch();
    this->m_data = "";
    this->m_prevHash = "";
    this->m_hash = "";
}

Block::Block(const Block &block) {
    this->m_type = block.getType();
    this->m_index = block.getIndex();
    this->m_date = block.getDate();
    this->m_data = block.getData();
    this->m_prevHash = block.getPrevHash();
    this->m_hash = block.getHash();
    this->m_signatures = block.m_signatures;
    this->m_transactions = block.m_transactions;
}

Block::Block(
    std::string &&type,
    std::string &&data,
    BigNumber idx,
    long long date,
    std::string &&prevHash,
    std::string &&hash,
    std::vector<Approvers> &&signatures,
    std::vector<Transaction> &&transactions)
    : m_data(std::move(data))
    , m_index(std::move(idx))
    , m_date(date)
    , m_prevHash(std::move(prevHash))
    , m_hash(std::move(hash))
    , m_signatures(std::move(signatures))
    , m_transactions(std::move(transactions)) {
    if (type != "genesis")
        setType(type);
}

Block Block::createBlockFromSerialized(const std::string &serialized) {
    Block block;
    block.deserialize(serialized);
    return block;
}

Block::~Block() {
}

Block Block::operator=(const Block &block) {
    m_type = block.m_type;
    m_data = block.m_data;
    m_index = block.m_index;
    m_date = block.m_date;
    m_prevHash = block.m_prevHash;
    m_hash = block.m_hash;
    m_signatures = block.m_signatures;
    m_transactions = block.m_transactions;
    return *this;
}

void Block::calcHash() {
    SHA3 sha3(SHA3::Bits::Bits512);
    std::string index = m_index.toStdString(16);
    sha3.add(index.c_str(), index.size());
    sha3.add(m_data.c_str(), m_data.size());

    for (const auto &tx : std::as_const(m_transactions)) {
        auto txSerialize = tx.serialize();
        sha3.add(txSerialize.c_str(), txSerialize.size());
    }

    this->m_hash = sha3.getHash();
}

void Block::setType(BlockType value) {
    m_type = value;
}

void Block::setType(const std::string &value) {
    if (value == "data") {
        m_type = BlockType::Data;
    } else if (value == "datamerge") {
        m_type = BlockType::DataMerge;
        // } else if (value == "genesismerge") {
        //     m_type = BlockType::GenesisMerge;
    } else if (value == "dummy") {
        m_type = BlockType::Dummy;
    } else {
        qFatal("Wrong block type");
    }
}

void Block::setPrev(const BlockVariant &prev) {
    if (prev.isEmpty()) {
        // qDebug() << "[Block] Construction first block";
        this->m_index = BigNumber("0");
        this->m_prevHash = Utils::calcHash("0 index");
    } else {
        // qDebug() << "[Block] Construction block. Previous block id: " << prev->getIndex();
        this->m_index = prev.getIndex() + 1;
        this->m_prevHash = prev.getHash();
    }
}

void Block::addTransaction(const Transaction &transaction) {
    if (!Utils::vector_contains(m_transactions, transaction)) {
        m_transactions.push_back(transaction);
    }
}

void Block::addTransactions(const std::vector<Transaction> &transactions) {
    for (const auto &transaction : std::as_const(transactions)) {
        addTransaction(transaction);
    }
}

const std::string &Block::getDataForSignature() const {
    return m_hash;
}

void Block::sign(const Actor<KeyPrivate> &actor) {
    calcHash();
    std::string sign = actor.key().sign(getDataForSignature());
    this->m_signatures.push_back({ actor.id().toStdString(), sign, true });
}

bool Block::verify(const Actor<KeyPublic> &actor) const {
    bool res = actor.key().verify(getDataForSignature(), getSignature());
    return m_signatures.empty() ? false : res;
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
    return m_hash == block.getHash();
}

BlockCompare Block::compareBlock(const Block &b) const {
    BlockCompare temp;
    temp.approverDiff = BigNumber(getApprover().toStdString()) - BigNumber(b.getApprover().toStdString());
    temp.indexDiff = getIndex() - b.getIndex();
    temp.dataDiff =
        Utils::compare(QByteArray::fromStdString(getData()), QByteArray::fromStdString(b.getData()));
    temp.digitalSigDiff = getSignature() == b.getSignature();
    temp.hashDiff = getHash() == b.getHash();
    temp.prevHashDiff = getPrevHash() == b.getPrevHash();
    return temp;
}

void Block::addData(const std::string &data) {
    std::vector<std::string> v = Serialization::deserialize(this->m_data);
    if (!Utils::vector_contains(v, data)) {
        v.push_back(data);
    }
    this->m_data = Serialization::serialize(v);
}

void Block::addDatas(const std::vector<std::string> &datas) {
    for (const auto &data : datas) {
        addData(data);
    }
}

void Block::initializeData(const std::string &serializedData) {
    this->m_data = serializedData;
}

std::vector<Transaction> Block::extractTransactions() const {
    if (m_type != BlockType::Data) {
        qFatal("Wrong transaction extract?");
        return {};
    }

    return transactions();
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
    return this->getHash().empty() && this->getSignature().empty() && this->getPrevHash().empty();
}

BlockType Block::getType() const {
    return m_type;
}

std::string Block::getTypeStr() const {
    auto type = std::string(magic_enum::enum_name(m_type));
    return Utils::str_to_lower(type);
}

std::string Block::getSignature() const {
    return m_signatures.empty() ? "" : this->m_signatures.begin()->sign;
}

const std::vector<Approvers> &Block::signatures() const {
    return m_signatures;
}

const std::vector<Transaction> &Block::transactions() const {
    return m_transactions;
}

QByteArrayList Block::getListSignatures() const {
    QByteArrayList res;

    for (auto const &signature : m_signatures) {
        res << QByteArray::fromStdString(signature.actorId) << QByteArray::fromStdString(signature.sign)
            << (signature.isApprove ? "1" : "0");
    }

    return res;
}

void Block::addSignature(const QByteArray &id, const QByteArray &sign, const bool &isApprover) {
    this->m_signatures.push_back({ id.toStdString(), sign.toStdString(), isApprover });
}

void Block::setIndex(const BigNumber &index) {
    m_index = index;
}

void Block::setPrevHash(const std::string &value) {
    m_prevHash = value;
}

ActorId Block::getApprover() const {
    if (m_signatures.empty()) {
        return ActorId();
    } else {
        for (int i = m_signatures.size() - 1; i >= 0; i--) {
            if (m_signatures[i].isApprove)
                return m_signatures[i].actorId;
        }
    }

    return ActorId();
}

BigNumber Block::getIndex() const {
    return m_index;
}

std::string Block::getData() const {
    return m_data;
}

std::vector<std::string> Block::getDatas() const {
    return Serialization::deserialize(this->m_data);
}

std::string Block::getHash() const {
    return m_hash;
}

std::string Block::getPrevHash() const {
    return m_prevHash;
}

bool Block::operator<(const Block &other) {
    if (this->m_index < other.getIndex()) {
        return true;
    } else if (this->m_data < other.getData()) {
        return true;
    }
    return false;
}

bool Block::isApprover(const ActorId &actorId) const {
    return actorId == getApprover();
}

long long Block::getDate() const {
    return m_date;
}

void Block::setDate(long long value) {
    m_date = value;
}
