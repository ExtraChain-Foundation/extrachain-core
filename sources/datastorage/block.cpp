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
    this->m_type     = BlockType::Data;
    this->m_index    = BigNumber(-1);
    this->m_date     = QDateTime::currentDateTime().toMSecsSinceEpoch();
    this->m_prevHash = "";
    this->m_hash     = "";
}

Block::Block(const Block &block) {
    this->m_type         = block.getType();
    this->m_index        = block.getIndex();
    this->m_date         = block.getDate();
    this->m_dataService  = block.dataService();
    this->m_prevHash     = block.getPrevHash();
    this->m_hash         = block.getHash();
    this->m_signatures   = block.m_signatures;
    this->m_transactions = block.m_transactions;
}

Block::Block(
    std::string           &&type,
    std::string           &&data,
    BigNumber               idx,
    long long               date,
    std::string           &&prevHash,
    std::string           &&hash,
    Signatures            &&signatures,
    std::set<Transaction> &&transactions)
    : m_index(std::move(idx))
    , m_date(date)
    , m_prevHash(std::move(prevHash))
    , m_hash(std::move(hash))
    , m_signatures(std::move(signatures))
    , m_transactions(std::move(transactions)) {
    if (type != "genesis")
        setType(type);
    setDataServiceFromMessagePack(data);
}

Block::~Block() {
}

Block Block::operator=(const Block &block) {
    m_type         = block.m_type;
    m_dataService  = block.m_dataService;
    m_index        = block.m_index;
    m_date         = block.m_date;
    m_prevHash     = block.m_prevHash;
    m_hash         = block.m_hash;
    m_signatures   = block.m_signatures;
    m_transactions = block.m_transactions;
    return *this;
}

void Block::calcHash() {
    SHA3        sha3(SHA3::Bits::Bits512);
    std::string index = m_index.toStdString(NumeralBase::Hex);
    sha3.add(index.c_str(), index.size());

    for (const auto &data : m_dataService) {
        sha3.add(data.c_str(), data.size());
    }

    for (const auto &tx : std::as_const(m_transactions)) {
        auto txHash = tx.hash();
        sha3.add(txHash.c_str(), txHash.size());
    }

    this->m_hash = sha3.getHash();
}

void Block::setType(BlockType value) {
    if (value == BlockType::Genesis) {
        qFatal("Block: try to set not data type");
    }

    m_type = value;
}

void Block::setType(const std::string &value) {
    if (value == "data") {
        m_type = BlockType::Data;
    } else if (value == "dummy") {
        m_type = BlockType::Dummy;
    } else {
        qFatal("Block: try to set not data type");
    }
}

void Block::setPrev(const BlockVariant &prev) {
    if (prev.isEmpty()) {
        // qDebug() << "[Block] Construction first block";
        this->m_index    = BigNumber("0");
        this->m_prevHash = Utils::calcHash("0 index");
    } else {
        // qDebug() << "[Block] Construction block. Previous block id: " << prev->getIndex();
        this->m_index    = prev.getIndex() + 1;
        this->m_prevHash = prev.getHash();
    }
}

void Block::addTransaction(const Transaction &transaction) {
    m_transactions.insert(transaction);
}

void Block::addTransactions(const std::set<Transaction> &transactions) {
    for (const auto &transaction : std::as_const(transactions)) {
        addTransaction(transaction);
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

void Block::sign(const std::shared_ptr<Actor<KeyPrivate>> actor) {
    calcHash();
    std::string sign = Utils::bytesEncodeVec(actor->key().sign(getDataForSignature()));
    this->addSignature(actor->id().toStdString(), sign, true);
}

BlockSignError Block::verify(const Actor<KeyPublic> &actor) const {
    if (m_signatures.empty()) {
        return BlockSignError::EmptySignatures;
    }

    auto it = this->m_signatures.find(actor.id().toStdString());

    if (it == this->m_signatures.end()) {
        return BlockSignError::NoActorSignature;
    }

    auto signStr = it->second;
    auto sign    = Utils::bytesDecodeVec<crypto_sign_BYTES>(signStr);
    bool res     = actor.key().verify(getDataForSignature(), sign);

    if (!res) {
        return BlockSignError::InvalidSignature;
    }

    return BlockSignError::NoError;
}

bool Block::equals(const Block &block) const {
    return m_hash == block.getHash();
}

void Block::addData(const std::string &data) {
    m_dataService.insert(data);
}

void Block::addDatas(const std::set<std::string> &datas) {
    for (const auto &data : datas) {
        addData(data);
    }
}

void Block::addDatas(const std::vector<std::string> &datas) {
    for (const auto &data : datas) {
        addData(data);
    }
}

Transaction Block::getTransactionByHash(std::string hash) const {
    for (const auto &tx : m_transactions)
        if (tx.hash() == hash)
            return tx;
    return Transaction();
}

QString Block::toString() const {
    return QString::fromStdString(this->toStdString());
}

std::string Block::toStdString() const {
    std::ostringstream oss;

    oss << "Block { "
        << "type: " << Utils::enumFullName(m_type) << ", "
        << "data service: [" << m_dataService.size() << "], "
        << "index: " << m_index.toStdString() << " (" << m_index.toStdString(NumeralBase::Dec) << "), "
        << "date: " << QDateTime::fromMSecsSinceEpoch(m_date).toString().toStdString() << ", "
        << "prev_hash: '"
        << (m_prevHash.length() > 10 ? m_prevHash.substr(0, 5) + "..."
                                           + m_prevHash.substr(m_prevHash.size() - 5, m_prevHash.size() - 1)
                                     : m_prevHash)
               + "', "
        << "hash: '"
        << (m_hash.length() > 10
                ? m_hash.substr(0, 5) + "..." + m_hash.substr(m_hash.size() - 5, m_hash.size() - 1)
                : m_hash)
        << "', "
        << "signatures: [" << m_signatures.size() << "], "
        << "transactions: [" << m_transactions.size() << "]"
        << " }";

    return oss.str();
}

bool Block::isEmpty() const {
    return m_index < 0 && this->getHash().empty() && this->m_signatures.empty()
           && (m_index == 0 || this->getPrevHash().empty());
}

BlockType Block::getType() const {
    return m_type;
}

std::string Block::getTypeStr() const {
    auto type = std::string(magic_enum::enum_name(m_type));
    return Utils::str_to_lower(type);
}

const Signatures &Block::signatures() const {
    return m_signatures;
}

const Transactions &Block::transactions() const {
    if (m_type == BlockType::Genesis) {
        qFatal("GenesisBlock: try to use transactions");
    }

    return m_transactions;
}

void Block::addSignature(const ActorId &id, const std::string &sign, bool isApprove) {
    if (this->m_signatures.size() < Config::DataStorage::MAX_SIGN_AMOUNT
        || this->m_signatures.find(id) != this->m_signatures.end()) {
        this->m_signatures[id] = sign;
    }
}

void Block::addSignatures(const Signatures &approvers) {
    for (const auto &[key, row] : approvers) {
        m_signatures[key] = row;
    }
}

void Block::clearSignatures() {
    m_signatures.clear();
}

void Block::setIndex(const BigNumber &index) {
    m_index = index;
}

void Block::setPrevHash(const std::string &value) {
    m_prevHash = value;
}

BigNumber Block::getIndex() const {
    return m_index;
}

const std::set<std::string> &Block::dataService() const {
    return m_dataService;
}

std::string Block::getDataMessagePack() const {
    return MessagePack::serialize(m_dataService);
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
    } else if (this->m_dataService < other.dataService()) {
        return true;
    }
    return false;
}

bool Block::isApprover(const ActorId &actorId) const {
    return false;
}

long long Block::getDate() const {
    return m_date;
}

void Block::setDate(long long value) {
    m_date = value;
}

void Block::setDataServiceFromMessagePack(const std::string &value) {
    // if (m_dataService.empty())
    // return;
    m_dataService = MessagePack::deserialize<std::set<std::string>>(value);
}

QDebug operator<<(QDebug debug, const Approver &approver) {
    QDebugStateSaver saver(debug);
    debug.nospace().noquote() << approver.toStdString();
    return debug;
}

QDebug operator<<(QDebug debug, const Block &block) {
    QDebugStateSaver saver(debug);
    debug.nospace().noquote() << block.toStdString();
    return debug;
}
