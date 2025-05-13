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
    BalanceOnlyFirstSection
};
// FORMAT_ENUM(TransactionProveError)

class EXTRACHAIN_EXPORT Transaction {
private:
    ActorId                    m_sender;                               // sender address
    ActorId                    m_receiver;                             // receiver address
    BigNumberFloat             m_amount;                               // coin amount
    std::optional<std::string> m_data;                                 // additional payload field
    ActorId                    m_token;                                // token contract address
    BigNumber                  m_section = BigNumber("-1");            // section id at the moment of tx creation
    std::string                m_hash;                                 // hash from all fields
    Signature                  m_signature = Signature();              // digital signature
    TransactionType            m_type      = TransactionType::Regular; // transaction type
    std::uint64_t              timestamp_;
    std::set<std::string>      prev_hashs_;

public:
    // Construct empty transaction
    Transaction();

    // Construct transaction
    Transaction(const ActorId        &sender,
                const ActorId        &receiver,
                const BigNumberFloat &amount,
                const ActorId        &token = ActorId(),
                const std::string    &data  = std::string());

    Transaction(const Transaction &other);

    Transaction(Transaction &&other) noexcept;

    // digital signature
    bool sign(const Actor<KeyPrivate> &actor);
    bool verify(const Actor<KeyPublic> &actor) const;

    // void setSenderBalance(BigNumber balance);
    // void setReceiverBalance(BigNumber balance);
    void set_section(const BigNumber &value);
    void setSignature(const Signature &value);
    void setHash(const std::string &value);

    ActorId                    sender() const;
    ActorId                    receiver() const;
    BigNumberFloat             amount() const;
    BigNumber                  section() const;
    std::optional<std::string> data() const;
    std::string                hash() const;
    ActorId                    token() const;
    TransactionType            type() const;
    std::uint64_t              timestamp() const {
        return timestamp_;
    }
    std::set<std::string> prev_hash() const {
        return prev_hashs_;
    }
    Signature signature() const;

    /**
     * Calculates hash of this block and writes hash to "hash" variable.
     * Uses blake3.
     */
    void calculate_hash();

    virtual bool isEmpty() const;
    virtual bool isBurn() const;
    bool         isSigned() const;
    bool         operator<(const Transaction &other) const;

    bool         operator==(const Transaction &transaction) const;
    void         operator=(const Transaction &transaction);
    Transaction &operator=(Transaction &&other) noexcept;

    void setToken(const ActorId &value);
    void setData(const std::string &value);
    void setAmount(const BigNumberFloat &value);
    void setSender(const ActorId &value);
    void setReceiver(const ActorId &value);
    bool isRewardTransaction() const;
    bool isConversionTransaction() const;
    void setType(TransactionType newType);
    void set_timestamp(std::uint64_t new_timestamp) {
        this->timestamp_ = new_timestamp;
    }
    void set_prev_hashs(const std::set<std::string> &prev_hashs) {
        this->prev_hashs_ = prev_hashs;
    }
    void insert_prev_hash(const std::string hash) {
        this->prev_hashs_.insert(hash);
    }

    BOOST_DESCRIBE_CLASS(Transaction,
                         (),
                         (),
                         (),
                         (m_section,
                          m_type,
                          m_sender,
                          m_receiver,
                          m_token,
                          m_amount,
                          timestamp_,
                          m_data,
                          prev_hashs_,
                          m_hash,
                          m_signature))
};

struct TransactionInfo {
    TransactionAmountOperation operation = TransactionAmountOperation::Plus;
    Transaction                transaction;
};
BOOST_DESCRIBE_STRUCT(TransactionInfo, (), (operation, transaction))
