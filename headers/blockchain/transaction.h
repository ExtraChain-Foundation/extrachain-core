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

#pragma once

#include "blockchain/actor.h"
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"

enum class TransactionType {
    Genesis      = 0,
    Regular      = 1,
    InitContract = 2,
    Repeatable   = 3,
    Reward       = 4,
    Burn         = 5,
    Conversion   = 6,
    Balance      = 99,
    Unknown      = 100
};
MSGPACK_ADD_ENUM(TransactionType)

enum class TransactionError {
    Unknown,
    NoSender,
    EmptyTransaction,
    NoLastBlock, // EmptyBlockchain?
    InsufficientFunds,
    NoCurrentUser,
    ZeroAmount
};

enum class TransactionAmountOperation {
    Plus,
    Minus
};

enum class TransactionProveError {
    NoError,
    Unknown,
    Duplicate,
    WrongHash,
    AmountZero,              // amount == 0
    AmountLessZero,          // amount less 0
    IdenticalSenderReceiver, // sender == receiver
    NotIdenticalSenderReceiver,
    EmptyBlockchain, // no real block
    SenderZero,
    ReceiverZero,
    SenderNotExists,        // sender is not exist
    ReceiverNotExists,      // receiver is not exist
    SenderBalanceBelowZero, // sender's balance will be < 0
    SelfPleasure,
    MissingSignature,
    InvalidSignature,
    RewardInvalidToken,
    InvalidTokenCount,
    BurnIncorrectReceiver,
    ConversionIncorrectFromToken,
    ConversionIncorrectBalance,
    ConversionEqualToken,
    NoSectionAdded,
    GenesisOnlyZeroSection,
    SectionTooBig,
    BalanceOnlyFirstSection,
    TooSectionDiff
};
// FORMAT_ENUM(TransactionProveError)

class EXTRACHAIN_EXPORT Transaction {
private:
    ActorId                    sender_;                               // sender address
    ActorId                    receiver_;                             // receiver address
    BigNumberFloat             amount_;                               // coin amount
    std::optional<std::string> meta_;                                 // additional payload field
    ActorId                    token_;                                // token contract address
    BigNumber                  section_ = BigNumber("-1");            // section id at the moment of tx creation
    std::string                hash_;                                 // hash from all fields
    Signature                  signature_ = Signature();              // digital signature
    TransactionType            type_      = TransactionType::Regular; // transaction type
    std::uint64_t              timestamp_;
    std::set<std::string>      prev_hashs_;

public:
    // Construct empty transaction
    Transaction();

    Transaction(const Transaction &other);

    Transaction(Transaction &&other) noexcept;

    // for signature
    bool sign(const Actor<KeyPrivate> &actor);
    bool verify(const Actor<KeyPublic> &actor) const;

    ActorId                    sender() const;
    ActorId                    receiver() const;
    BigNumberFloat             amount() const;
    BigNumber                  section() const;
    std::optional<std::string> meta() const;
    std::string                hash() const;
    ActorId                    token() const;
    TransactionType            type() const;
    std::uint64_t              timestamp() const;
    std::set<std::string>      prev_hashs() const;
    Signature                  signature() const;

    /**
     * Calculates hash of this transaction and writes hash to "hash" variable.
     * Uses blake3.
     */
    std::string calculate_hash() const;

    void update_hash();

    virtual bool is_empty() const;
    virtual bool is_burn() const;
    bool         is_signed() const;
    bool         operator<(const Transaction &other) const;

    bool         operator==(const Transaction &transaction) const;
    void         operator=(const Transaction &transaction);
    Transaction &operator=(Transaction &&other) noexcept;

    void set_section(const BigNumber &value);
    void set_type(TransactionType newType);
    void set_sender(const ActorId &value);
    void set_receiver(const ActorId &value);
    void set_token(const ActorId &value);
    void set_amount(const BigNumberFloat &value);
    void set_timestamp(std::uint64_t new_timestamp);
    void set_meta(const std::string &value);
    void set_prev_hashs(const std::set<std::string> &prev_hashs);

    void insert_prev_hash(const std::string hash) {
        this->prev_hashs_.insert(hash);
    }

    BOOST_DESCRIBE_CLASS(
        Transaction,
        (),
        (),
        (),
        (section_, type_, sender_, receiver_, token_, amount_, timestamp_, meta_, prev_hashs_, hash_, signature_))
};

struct TransactionInfo {
    TransactionAmountOperation operation = TransactionAmountOperation::Plus;
    Transaction                transaction;
};
BOOST_DESCRIBE_STRUCT(TransactionInfo, (), (operation, transaction))
