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

#include "chain/transaction.h"

namespace {

    void append_field(std::string &target, std::string_view value) {
        auto size = static_cast<std::uint32_t>(value.size());
        for (int shift = 24; shift >= 0; shift -= 8) {
            target.push_back(static_cast<char>(size >> shift));
        }
        target.append(value);
    }

    std::string contract_hash_data(const Transaction &transaction) {
        std::string result = "EXTRACHAIN:CONTRACT-TRANSACTION:2";
        append_field(result, transaction.section().to_string());
        append_field(result, std::to_string(std::to_underlying(transaction.type())));
        append_field(result, transaction.sender().to_string());
        append_field(result, transaction.receiver().to_string());
        append_field(result, transaction.token().to_string());
        append_field(result, transaction.amount().to_string());
        append_field(result, std::to_string(transaction.timestamp()));
        append_field(result, transaction.meta().value_or(""));
        append_field(result, std::to_string(transaction.prev_hashs().size()));
        for (const auto &previous_hash : transaction.prev_hashs()) {
            append_field(result, previous_hash);
        }
        return result;
    }

} // namespace

Transaction::Transaction() {
    this->sender_    = ActorId();
    this->receiver_  = ActorId();
    this->token_     = ActorId();
    this->amount_    = BigNumberFloat(0);
    this->timestamp_ = 0;
    this->meta_      = std::nullopt;
    this->section_   = BigNumber(0);
    this->hash_      = "";
    this->signature_ = Signature();
    this->type_      = TransactionType::Regular;
    update_hash();
}

Transaction::Transaction(const Transaction &other) {
    this->sender_     = other.sender_;
    this->receiver_   = other.receiver_;
    this->amount_     = other.amount_;
    this->timestamp_  = other.timestamp_;
    this->meta_       = other.meta_;
    this->token_      = other.token_;
    this->section_    = other.section_;
    this->hash_       = other.hash_;
    this->signature_  = other.signature_;
    this->type_       = other.type_;
    this->prev_hashs_ = other.prev_hashs_;
    update_hash();
}

Transaction::Transaction(Transaction &&other) noexcept {
    sender_     = std::move(other.sender_);
    receiver_   = std::move(other.receiver_);
    amount_     = std::move(other.amount_);
    timestamp_  = other.timestamp_;
    meta_       = std::move(other.meta_);
    token_      = std::move(other.token_);
    section_    = std::move(other.section_);
    hash_       = std::move(other.hash_);
    signature_  = std::move(other.signature_);
    type_       = std::move(other.type_);
    prev_hashs_ = std::move(other.prev_hashs_);
    update_hash();

    other.hash_ = "";
}

void Transaction::set_receiver(const ActorId &value) {
    receiver_ = value;
}

void Transaction::set_sender(const ActorId &value) {
    sender_ = value;
}

void Transaction::set_amount(const BigNumberFloat &value) {
    amount_ = value;
}

void Transaction::set_timestamp(std::uint64_t new_timestamp) {
    this->timestamp_ = new_timestamp;
}

void Transaction::set_meta(const std::string &value) {
    meta_ = value;
}

void Transaction::set_prev_hashs(const std::set<std::string> &prev_hashs) {
    this->prev_hashs_ = prev_hashs;
}

void Transaction::set_token(const ActorId &value) {
    token_ = value;
}

std::string Transaction::calculate_hash() const {
    if (is_contract_transaction(type_)) {
        return Utils::calculate_hash(contract_hash_data(*this));
    }

    auto hashData =
        section_.to_string() + std::to_string(std::to_underlying(type_)) + sender_.to_string()
        + receiver_.to_string() + token_.to_string() + amount_.to_string() + std::to_string(timestamp_)
        + (meta_.has_value() ? meta_.value() : ""); // TODO: + amount.size() meta.size() + prev_hashs_.size()
                                                    // TODO: meta max 255 in prove + section size?

    for (const auto &prev_hash : prev_hashs_) {
        hashData += prev_hash;
    }

    return Utils::calculate_hash(hashData);
}

std::string Transaction::calculate_hash_hex() const {
    auto hashData = section_.to_hex_string() + std::to_string(std::to_underlying(type_)) + sender_.to_string()
                    + receiver_.to_string() + token_.to_string() + amount_.to_hex_string()
                    + std::to_string(timestamp_) + (meta_.has_value() ? meta_.value() : "");

    for (const auto &prev_hash : prev_hashs_) {
        hashData += prev_hash;
    }

    return Utils::calculate_hash(hashData);
}

void Transaction::update_hash() {
    std::string resultHash = calculate_hash();
    this->hash_            = resultHash;
}

TransactionType Transaction::type() const {
    return type_;
}

std::uint64_t Transaction::timestamp() const {
    return timestamp_;
}

std::set<std::string> Transaction::prev_hashs() const {
    return prev_hashs_;
}

void Transaction::set_type(TransactionType newType) {
    type_ = newType;
}

bool Transaction::sign(const Actor<KeyPrivate> &actor) {
    if (this->sender_ != actor.id()) {
        return false;
    }

    // Sign with the LEGACY hex-form hash for wire compatibility: pre-decimal
    // peers re-derive the hash in hex and expect the signature to match that.
    // New peers always accept either hex or decimal in verify(), so we don't
    // lose anything on the new side.
    //
    // Once we stop talking to legacy peers, switch back to calculate_hash().
    this->hash_ = calculate_hash_hex();
    auto sign   = actor.key().sign(hash_);
    if (!sign.has_value()) {
        return false;
    }
    this->signature_ = sign.value();
    return true;
}

bool Transaction::verify(const Actor<KeyPublic> &actor) const {
    if (Utils::is_container_empty(signature_)) {
        return false;
    }

    // New signatures are computed over the decimal form. Transactions signed before
    // the hex → decimal migration still validate against the legacy hex form.
    auto verify_primary = actor.key().verify(calculate_hash(), signature_);
    if (verify_primary.has_value() && verify_primary.value()) {
        return true;
    }

    auto verify_legacy = actor.key().verify(calculate_hash_hex(), signature_);
    if (verify_legacy.has_value() && verify_legacy.value()) {
        return true;
    }

    return false;
}

void Transaction::set_section(const BigNumber &value) {
    this->section_ = value;

    update_hash();
}

ActorId Transaction::sender() const {
    return this->sender_;
}

ActorId Transaction::receiver() const {
    return this->receiver_;
}

BigNumberFloat Transaction::amount() const {
    return this->amount_;
}

SectionId Transaction::section() const {
    return this->section_;
}

std::string Transaction::hash() const {
    return this->hash_;
}

TokenId Transaction::token() const {
    return this->token_;
}

std::optional<std::string> Transaction::meta() const {
    return this->meta_;
}

Signature Transaction::signature() const {
    return this->signature_;
}

bool Transaction::is_empty() const {
    return sender_.is_zero() && receiver_.is_zero() && amount_ <= 0 && section_ == -1 && hash_.empty();
}

bool Transaction::is_burn() const {
    return sender_.is_zero() && amount_ <= 0 && section_ == -1 && hash_.empty();
}

bool Transaction::is_signed() const {
    return !Utils::is_container_empty(this->signature_);
}

bool Transaction::operator<(const Transaction &other) const {
    if (section_ != other.section_)
        return section_ < other.section_;
    if (timestamp_ != other.timestamp_)
        return timestamp_ < other.timestamp_;
    if (hash_ != other.hash_)
        return hash_ < other.hash_;
    if (sender_ != other.sender_)
        return sender_ < other.sender_;
    if (receiver_ != other.receiver_)
        return receiver_ < other.receiver_;
    if (amount_ != other.amount_)
        return amount_ < other.amount_;
    if (token_ != other.token_)
        return token_ < other.token_;
    if (meta_ != other.meta_)
        return meta_ < other.meta_;
    if (type_ != other.type_)
        return static_cast<int>(type_) < static_cast<int>(other.type_);
    if (prev_hashs_ != other.prev_hashs_) {
        return prev_hashs_ < other.prev_hashs_;
    }

    return std::lexicographical_compare(signature_.begin(),
                                        signature_.end(),
                                        other.signature_.begin(),
                                        other.signature_.end());
}

bool Transaction::operator==(const Transaction &transaction) const {
    if (this->section_ != transaction.section())
        return false;
    if (this->type() != transaction.type())
        return false;
    if (this->sender_ != transaction.sender())
        return false;
    if (this->receiver_ != transaction.receiver())
        return false;
    if (this->token_ != transaction.token())
        return false;
    if (this->amount_ != transaction.amount()) {
        // return false; // must be commented
    }
    if (this->timestamp_ != transaction.timestamp())
        return false;
    if (this->meta_ != transaction.meta())
        return false;
    if (this->prev_hashs_ != transaction.prev_hashs())
        return false;

    return true;
}

void Transaction::operator=(const Transaction &other) {
    if (this == &other) {
        return;
    }

    this->sender_     = other.sender_;
    this->receiver_   = other.receiver_;
    this->amount_     = other.amount_;
    this->timestamp_  = other.timestamp_;
    this->meta_       = other.meta_;
    this->token_      = other.token_;
    this->section_    = other.section_;
    this->hash_       = other.hash_;
    this->signature_  = other.signature_;
    this->type_       = other.type_;
    this->prev_hashs_ = other.prev_hashs_;

    update_hash();
}

Transaction &Transaction::operator=(Transaction &&other) noexcept {
    if (this == &other) {
        return *this;
    }

    sender_     = std::move(other.sender_);
    receiver_   = std::move(other.receiver_);
    amount_     = std::move(other.amount_);
    timestamp_  = std::move(other.timestamp_);
    meta_       = std::move(other.meta_);
    token_      = std::move(other.token_);
    section_    = std::move(other.section_);
    hash_       = std::move(other.hash_);
    signature_  = std::move(other.signature_);
    type_       = std::move(other.type_);
    prev_hashs_ = std::move(other.prev_hashs_);
    update_hash();

    other.hash_      = "";
    other.signature_ = {};
    return *this;
}
