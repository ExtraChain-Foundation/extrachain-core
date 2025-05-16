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

#include "blockchain/transaction.h"

Transaction::Transaction() {
    this->m_sender    = ActorId();
    this->m_receiver  = ActorId();
    this->m_token     = ActorId();
    this->m_amount    = BigNumberFloat(0);
    this->timestamp_  = 0;
    this->meta_       = std::nullopt;
    this->m_section   = BigNumber(0);
    this->m_hash      = "";
    this->m_signature = Signature();
    this->m_type      = TransactionType::Regular;
    calculate_hash();
}

Transaction::Transaction(const ActorId        &sender,
                         const ActorId        &receiver,
                         const BigNumberFloat &amount,
                         const ActorId        &token,
                         const std::string    &data) {
    this->m_sender    = sender;
    this->m_receiver  = receiver;
    this->m_amount    = amount;
    this->meta_       = !data.empty() ? std::make_optional(data) : std::nullopt;
    this->m_section   = BigNumber(0);
    this->m_hash      = "";
    this->m_signature = Signature();
    this->m_type      = TransactionType::Regular;
    this->m_token     = token;
    calculate_hash();
}

Transaction::Transaction(const Transaction &other) {
    this->m_sender    = other.m_sender;
    this->m_receiver  = other.m_receiver;
    this->m_amount    = other.m_amount;
    this->timestamp_  = other.timestamp_;
    this->meta_       = other.meta_;
    this->m_token     = other.m_token;
    this->m_section   = other.m_section;
    this->m_hash      = other.m_hash;
    this->m_signature = other.m_signature;
    this->m_type      = other.m_type;
    this->prev_hashs_ = other.prev_hashs_;
    calculate_hash();
}

Transaction::Transaction(Transaction &&other) noexcept {
    m_sender    = std::move(other.m_sender);
    m_receiver  = std::move(other.m_receiver);
    m_amount    = std::move(other.m_amount);
    timestamp_  = other.timestamp_;
    meta_       = std::move(other.meta_);
    m_token     = std::move(other.m_token);
    m_section   = std::move(other.m_section);
    m_hash      = std::move(other.m_hash);
    m_signature = std::move(other.m_signature);
    m_type      = std::move(other.m_type);
    prev_hashs_ = std::move(other.prev_hashs_);
    calculate_hash();

    other.m_hash = "";
}

void Transaction::setReceiver(const ActorId &value) {
    m_receiver = value;
}

bool Transaction::isRewardTransaction() const {
    return m_type == TransactionType::Reward;
}

bool Transaction::isConversionTransaction() const {
    return m_type == TransactionType::Conversion;
}

void Transaction::setSignature(const Signature &value) {
    m_signature = value;
}

void Transaction::setHash(const std::string &value) {
    m_hash = value;
}

void Transaction::setSender(const ActorId &value) {
    m_sender = value;
}

void Transaction::setAmount(const BigNumberFloat &value) {
    m_amount = value;
}

void Transaction::set_meta(const std::string &value) {
    meta_ = value;
}

void Transaction::setToken(const ActorId &value) {
    m_token = value;
}

void Transaction::calculate_hash() {
    auto hashData = m_section.to_string() + std::to_string(std::to_underlying(m_type)) + m_sender.to_string()
                    + m_receiver.to_string() + m_token.to_string() + m_amount.to_string(NumeralBase::Hex)
                    + std::to_string(timestamp_) + (meta_.has_value() ? meta_.value() : "");

    for (const auto &prev_hash : prev_hashs_) {
        hashData += prev_hash;
    }

    std::string resultHash = Utils::calculate_hash(hashData);
    if (!resultHash.empty()) {
        this->m_hash = resultHash;
    }
}

TransactionType Transaction::type() const {
    return m_type;
}

std::uint64_t Transaction::timestamp() const {
    return timestamp_;
}

std::set<std::string> Transaction::prev_hashs() const {
    return prev_hashs_;
}

void Transaction::setType(TransactionType newType) {
    m_type = newType;
}

bool Transaction::sign(const Actor<KeyPrivate> &actor) {
    if (this->m_sender != actor.id()) {
        return false;
    }

    calculate_hash();
    auto sign = actor.key().sign(m_hash);
    if (!sign.has_value()) {
        return false;
    }
    this->m_signature = sign.value();
    return true;
}

bool Transaction::verify(const Actor<KeyPublic> &actor) const {
    if (Utils::is_container_empty(m_signature)) {
        return false;
    }

    // calculate_hash();

    auto verify = actor.key().verify(m_hash, m_signature);
    if (!verify.has_value()) {
        return false;
    }
    return verify.value();
}

void Transaction::set_section(const BigNumber &value) {
    this->m_section = value;

    calculate_hash();
}

ActorId Transaction::sender() const {
    return this->m_sender;
}

ActorId Transaction::receiver() const {
    return this->m_receiver;
}

BigNumberFloat Transaction::amount() const {
    return this->m_amount;
}

BigNumber Transaction::section() const {
    return this->m_section;
}

std::string Transaction::hash() const {
    return this->m_hash;
}

ActorId Transaction::token() const {
    return this->m_token;
}

std::optional<std::string> Transaction::meta() const {
    return this->meta_;
}

Signature Transaction::signature() const {
    return this->m_signature;
}

bool Transaction::isEmpty() const {
    return m_sender.is_zero() && m_receiver.is_zero() && m_amount <= 0 && m_section == -1 && m_hash.empty();
}

bool Transaction::isBurn() const {
    return m_sender.is_zero() && m_amount <= 0 && m_section == -1 && m_hash.empty();
}

bool Transaction::isSigned() const {
    return !this->m_signature.empty();
}

bool Transaction::operator<(const Transaction &other) const {
    if (timestamp_ != other.timestamp_) {
        return timestamp_ < other.timestamp_;
    }

    if (m_hash != other.m_hash)
        return m_hash < other.m_hash;

    if (m_sender != other.m_sender)
        return m_sender < other.m_sender;

    if (m_receiver != other.m_receiver)
        return m_receiver < other.m_receiver;

    if (m_amount != other.m_amount)
        return m_amount < other.m_amount;

    if (m_token != other.m_token)
        return m_token < other.m_token;

    if (meta_ != other.meta_)
        return meta_ < other.meta_;

    if (m_type != other.m_type)
        return static_cast<int>(m_type) < static_cast<int>(other.m_type);

    if (prev_hashs_ != other.prev_hashs_) {
        return prev_hashs_ < other.prev_hashs_;
    }

    return std::lexicographical_compare(m_signature.begin(),
                                        m_signature.end(),
                                        other.m_signature.begin(),
                                        other.m_signature.end());
}

bool Transaction::operator==(const Transaction &transaction) const {
    if (this->type() != transaction.type()) {
        return false;
    }
    if (this->m_sender != transaction.sender())
        return false;
    if (this->m_receiver != transaction.receiver())
        return false;
    if (this->m_amount != transaction.amount()) {
        eLog("________ {} {}",
             this->amount().to_string(NumeralBase::Dec),
             transaction.amount().to_string(NumeralBase::Dec));
        // return false;
    }
    if (this->meta_ != transaction.meta())
        return false;
    if (this->m_token != transaction.token())
        return false;
    //    if (this->hash != transaction.getHash())
    //        return false;
    if (this->m_section != transaction.section())
        return false;
    if (this->prev_hashs_ != transaction.prev_hashs()) {
        return false;
    }
    if (this->timestamp_ != transaction.timestamp()) {
        return false;
    }
    //    if (this->signature != transaction.getSignature())
    //        return false;
    return true;
}

void Transaction::operator=(const Transaction &other) {
    this->m_sender    = other.m_sender;
    this->m_receiver  = other.m_receiver;
    this->m_amount    = other.m_amount;
    this->timestamp_  = other.timestamp_;
    this->meta_       = other.meta_;
    this->m_token     = other.m_token;
    this->m_section   = other.m_section;
    this->m_hash      = other.m_hash;
    this->m_signature = other.m_signature;
    this->m_type      = other.m_type;
    this->prev_hashs_ = other.prev_hashs_;
}

Transaction &Transaction::operator=(Transaction &&other) noexcept {
    if (this != &other) {
        m_sender    = std::move(other.m_sender);
        m_receiver  = std::move(other.m_receiver);
        m_amount    = std::move(other.m_amount);
        timestamp_  = std::move(other.timestamp_);
        meta_       = std::move(other.meta_);
        m_token     = std::move(other.m_token);
        m_section   = std::move(other.m_section);
        m_hash      = std::move(other.m_hash);
        m_signature = std::move(other.m_signature);
        m_type      = std::move(other.m_type);
        prev_hashs_ = std::move(other.prev_hashs_);
        calculate_hash();

        other.m_hash = {};
    }

    return *this;
}
