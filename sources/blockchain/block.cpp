/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#include "blockchain/block.h"
#include "blockchain/block_variant.h"

#include "blake3.h"

Block::Block() {
    this->m_type     = BlockType::Data;
    this->id_        = BigNumber(-1);
    this->m_date     = QDateTime::currentDateTime().toMSecsSinceEpoch();
    this->m_prevHash = "";
    this->m_hash     = "";
}

Block::Block(const Block &block) {
    this->m_type         = block.getType();
    this->id_            = block.id();
    this->m_date         = block.getDate();
    this->m_dataService  = block.dataService();
    this->m_prevHash     = block.getPrevHash();
    this->m_hash         = block.getHash();
    this->m_signatures   = block.m_signatures;
    this->m_transactions = block.m_transactions;
}

Block::Block(std::string           &&type,
             std::string           &&data,
             BigNumber               idx,
             std::uint64_t           date,
             std::string           &&prevHash,
             std::string           &&hash,
             Signatures            &&signatures,
             std::set<Transaction> &&transactions)
    : id_(std::move(idx))
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
    id_            = block.id_;
    m_date         = block.m_date;
    m_prevHash     = block.m_prevHash;
    m_hash         = block.m_hash;
    m_signatures   = block.m_signatures;
    m_transactions = block.m_transactions;
    return *this;
}

void Block::calculate_hash() {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    std::string index = id_.to_string(NumeralBase::Hex);
    blake3_hasher_update(&hasher, index.c_str(), index.size());

    for (const auto &data : m_dataService) {
        blake3_hasher_update(&hasher, data.c_str(), data.size());
    }

    for (const auto &tx : std::as_const(m_transactions)) {
        auto txHash = tx.hash();
        blake3_hasher_update(&hasher, txHash.c_str(), txHash.size());
    }

    uint8_t hash[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);
    this->m_hash = fmt::format("{:02x}", fmt::join(std::span(hash, BLAKE3_OUT_LEN), ""));
}

void Block::setType(BlockType value) {
    if (value == BlockType::Genesis) {
        eFatal("Block: try to set not data type");
    }

    m_type = value;
}

void Block::setType(const std::string &value) {
    if (value == "data") {
        m_type = BlockType::Data;
    } else if (value == "dummy") {
        m_type = BlockType::Dummy;
    } else {
        eFatal("Block: try to set not data type");
    }
}

void Block::setPrev(const BlockVariant &prev) {
    if (prev.isEmpty()) {
        // eLog("[Block] Construction first block");
        this->id_        = BigNumber("0");
        this->m_prevHash = Utils::calculate_hash("0 index");
    } else {
        // eLog("[Block] Construction block. Previous block id: {}", prev->id());
        this->id_        = prev.id() + 1;
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

void Block::sign(const Actor<KeyPrivate> &actor) {
    calculate_hash();
    auto sign = actor.key().sign(getDataForSignature());
    if (!sign.has_value()) {
        return;
    }
    this->addSignature(actor.id(), sign.value(), true);
}

BlockSignError Block::verify(const Actor<KeyPublic> &actor) const {
    if (m_signatures.empty()) {
        return BlockSignError::EmptySignatures;
    }

    auto it = this->m_signatures.find(actor.id());

    if (it == this->m_signatures.end()) {
        return BlockSignError::NoActorSignature;
    }

    auto res = actor.key().verify(getDataForSignature(), it->second);
    if (!res.has_value()) {
        return BlockSignError::InvalidSignature;
    }
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

bool Block::isEmpty() const {
    return id_ < 0 && this->getHash().empty() && this->m_signatures.empty()
           && (id_ == 0 || this->getPrevHash().empty());
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
        eFatal("GenesisBlock: try to use transactions");
    }

    return m_transactions;
}

void Block::addSignature(const ActorId &id, const Signature &sign, bool isApprove) {
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

void Block::set_id(const BigNumber &id) {
    id_ = id;
}

void Block::setPrevHash(const std::string &value) {
    m_prevHash = value;
}

BigNumber Block::id() const {
    return id_;
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
    if (this->id_ < other.id()) {
        return true;
    } else if (this->m_dataService < other.dataService()) {
        return true;
    }
    return false;
}

bool Block::isApprover(const ActorId &actorId) const {
    return false;
}

std::uint64_t Block::getDate() const {
    return m_date;
}

void Block::setDate(std::uint64_t value) {
    m_date = value;
}

void Block::setDataServiceFromMessagePack(const std::string &value) {
    // if (m_dataService.empty())
    // return;
    auto deserialized = MessagePack::deserialize<std::set<std::string>>(value);
    if (deserialized.has_value())
        m_dataService = deserialized.value();
}
