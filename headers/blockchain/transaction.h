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

#pragma once

#include "blockchain/actor.h"
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

enum class TransactionType {
    Regular      = 0,
    InitContract = 1,
    Reward       = 2
};
MSGPACK_ADD_ENUM(TransactionType)
FORMAT_ENUM(TransactionType)

enum class TransactionError {
    Unknown,
    EmptyTransaction,
    NoLastBlock, // EmptyBlockchain?
    InsufficientFunds,
    NoCurrentUser,
    ZeroAmount
};
// MSGPACK_ADD_ENUM(TransactionError)
FORMAT_ENUM(TransactionError)

enum class TransactionProveError {
    NoError,
    Unknown,
    AmountZero,              // amount == 0
    AmountLessZero,          // amount less 0
    IdenticalSenderReceiver, // sender == receiver
    EmptyBlockchain,         // no real block
    SenderNotExists,         // sender is not exist
    ReceiverNotExists,       // receiver is not exist
    ZeroProducer,            // producer 0
    ProducerVerify,          // bad signature in fee tx
    SenderBalanceBelowZero,  // sender's balance will be < 0
    SelfPleasure,
    MissingSignature,
    InvalidSignature,
    RewardInvalidToken,
    InvalidTokenCount
};
FORMAT_ENUM(TransactionProveError)

class EXTRACHAIN_EXPORT Transaction {
private:
    ActorId         m_sender;                          // sender address
    ActorId         m_receiver;                        // receiver address
    BigNumberFloat  m_amount;                          // coin amount
    std::uint64_t   m_date = 0;                        // transaction date
    std::string     m_data;                            // additional payload field
    ActorId         m_token;                           // token contract address
    BigNumber       m_prevBlock;                       // last block id at the moment of tx creation
    std::string     m_hash;                            // hash from all fields
    ActorId         m_approver;                        // address of the transaction approver.
    Signature       m_signature = Signature();         // digital signature
    ActorId         m_producer;                        // producer address
    TransactionType m_type = TransactionType::Regular; // transaction type

    /**
     * Calculates hash of this block and writes hash to "hash" variable.
     * Uses sha3.
     */
    void calcHash();

public:
    // Construct empty transaction
    Transaction();

    // Construct transaction
    Transaction(
        const ActorId        &sender,
        const ActorId        &receiver,
        const BigNumberFloat &amount,
        const ActorId        &token = ActorId(),
        const std::string    &data  = std::string());

    Transaction(const Transaction &other);

    Transaction(Transaction &&other) noexcept;

    // digital signature
    void sign(const std::shared_ptr<Actor<KeyPrivate>> actor);
    bool verify(const Actor<KeyPublic> &actor) const;

    // void setSenderBalance(BigNumber balance);
    // void setReceiverBalance(BigNumber balance);
    void setPrevBlock(const BigNumber &value);
    void setProducer(const ActorId &value);
    void setSignature(const Signature &value);
    void setApprover(const ActorId &value);
    void setHash(const std::string &value);

    ActorId        sender() const;
    ActorId        receiver() const;
    BigNumberFloat amount() const;
    BigNumber      prevBlock() const;
    std::uint64_t  date() const;
    std::string    data() const;
    std::string    hash() const;
    ActorId        token() const;
    ActorId        approver() const;
    Signature      signature() const;
    ActorId        producer() const;

    virtual bool isEmpty() const;
    virtual bool isBurn() const;
    bool         isSigned() const;
    auto         operator<=>(const Transaction &) const = default;
    bool         operator==(const Transaction &transaction) const;
    void         operator=(const Transaction &transaction);
    Transaction &operator=(Transaction &&other) noexcept;

    void            setDate(std::uint64_t value);
    void            setToken(const ActorId &value);
    void            setData(const std::string &value);
    void            setAmount(const BigNumberFloat &value);
    void            setSender(const ActorId &value);
    void            setReceiver(const ActorId &value);
    bool            isRewardTransaction() const;
    TransactionType type() const;
    virtual void    setType(TransactionType newType);

    MSGPACK_DEFINE(
        m_sender,
        m_receiver,
        m_amount,
        m_date,
        m_data,
        m_token,
        m_prevBlock,
        m_hash,
        m_approver,
        m_producer,
        m_signature,
        m_type)

    BOOST_DESCRIBE_CLASS(
        Transaction,
        (),
        (),
        (),
        (m_sender,
         m_receiver,
         m_amount,
         m_date,
         m_data,
         m_token,
         m_prevBlock,
         m_hash,
         m_approver,
         m_producer,
         m_signature,
         m_type))
};

MAKE_MAGICAL(Transaction)
