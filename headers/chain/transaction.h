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

#include "chain/actor.h"
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"

/**
 * @brief Types of chain transactions
 */
enum class TransactionType {
    Genesis      = 0,  ///< Initial chain transaction
    Regular      = 1,  ///< Standard value transfer
    InitContract = 2,  ///< Smart contract initialization
    Repeatable   = 3,  ///< Recurring transaction
    Reward       = 4,  ///< Mining/validation reward
    Burn         = 5,  ///< Token destruction
    Conversion   = 6,  ///< Token conversion
    Minting      = 7,  ///< Token minting (owner only)
    Balance      = 99, ///< Balance query transaction
    Unknown      = 100 ///< Unrecognized transaction type
};
MSGPACK_ADD_ENUM(TransactionType)

/**
 * @brief Transaction processing error codes
 */
enum class TransactionError {
    Unknown,           ///< Unspecified error
    NoSender,          ///< Missing sender information
    EmptyTransaction,  ///< Transaction contains no data
    NoLastSection,     ///< No previous section found
    InsufficientFunds, ///< Sender lacks required balance
    NoCurrentUser,     ///< No active user context
    ZeroAmount,        ///< Transaction amount is zero
    SubscriptionRowFull
};

/**
 * @brief Amount operation direction for transaction processing
 */
enum class TransactionAmountOperation {
    Plus, ///< Add amount to balance
    Minus ///< Subtract amount from balance
};

/**
 * @brief Transaction validation error codes
 */
enum class TransactionProveError {
    NoError,                      ///< Transaction is valid
    Unknown,                      ///< Unspecified validation error
    Duplicate,                    ///< Transaction already exists
    WrongHash,                    ///< Hash verification failed
    AmountZero,                   ///< Amount equals zero
    AmountLessZero,               ///< Amount is negative
    IdenticalSenderReceiver,      ///< Sender and receiver are the same
    NotIdenticalSenderReceiver,   ///< Sender and receiver must be identical
    EmptyChain,                   ///< No valid sections in chain
    SenderZero,                   ///< Sender address is zero
    ReceiverZero,                 ///< Receiver address is zero
    SenderNotExists,              ///< Sender account not found
    ReceiverNotExists,            ///< Receiver account not found
    SenderBalanceBelowZero,       ///< Transaction would create negative balance
    SelfPleasure,                 ///< Invalid self-transaction
    MissingSignature,             ///< Transaction lacks digital signature
    InvalidSignature,             ///< Digital signature verification failed
    RewardInvalidToken,           ///< Invalid token for reward transaction
    InvalidTokenCount,            ///< Token count validation failed
    BurnIncorrectReceiver,        ///< Wrong receiver for burn transaction
    ConversionIncorrectFromToken, ///< Invalid source token for conversion
    ConversionIncorrectBalance,   ///< Insufficient balance for conversion
    ConversionEqualToken,         ///< Source and target tokens are identical
    NoSectionAdded,               ///< Section information missing
    GenesisOnlyZeroSection,       ///< Genesis transaction must use section zero
    SectionTooBig,                ///< Section number exceeds limits
    BalanceOnlyFirstSection,      ///< Balance transactions limited to first section
    TooSectionDiff,               ///< Section difference too large
    BigReward,
    TooOften
};

/**
 * @brief Chain transaction representation
 *
 * Represents a single transaction in the chain containing all necessary
 * information for value transfer, smart contract execution, and validation.
 */
class EXTRACHAIN_EXPORT Transaction {
private:
    ActorId                    sender_;     ///< Transaction sender address
    ActorId                    receiver_;   ///< Transaction receiver address
    BigNumberFloat             amount_;     ///< Transaction amount
    std::optional<std::string> meta_;       ///< Optional metadata payload
    ActorId                    token_;      ///< Token contract address
    SectionId                  section_;    ///< Chain section ID
    std::string                hash_;       ///< Transaction hash (Blake3)
    Signature                  signature_;  ///< Digital signature
    TransactionType            type_;       ///< Transaction type
    std::uint64_t              timestamp_;  ///< Creation timestamp
    std::set<std::string>      prev_hashs_; ///< Previous transaction hashes

public:
    /**
     * @brief Default constructor - creates empty transaction
     */
    Transaction();

    /**
     * @brief Copy constructor
     * @param other Transaction to copy
     */
    Transaction(const Transaction &other);

    /**
     * @brief Move constructor
     * @param other Transaction to move
     */
    Transaction(Transaction &&other) noexcept;

    /**
     * @brief Sign transaction with private key
     * @param actor Actor containing private key for signing
     * @return true if signing successful, false otherwise
     */
    bool sign(const Actor<KeyPrivate> &actor);

    /**
     * @brief Verify transaction signature
     * @param actor Actor containing public key for verification
     * @return true if signature valid, false otherwise
     */
    bool verify(const Actor<KeyPublic> &actor) const;

    /**
     * @brief Get transaction sender
     * @return Sender actor ID
     */
    ActorId sender() const;

    /**
     * @brief Get transaction receiver
     * @return Receiver actor ID
     */
    ActorId receiver() const;

    /**
     * @brief Get transaction amount
     * @return Transaction amount as BigNumberFloat
     */
    BigNumberFloat amount() const;

    /**
     * @brief Get chain section
     * @return Section number as BigNumber
     */
    SectionId section() const;

    /**
     * @brief Get optional metadata
     * @return Optional metadata string
     */
    std::optional<std::string> meta() const;

    /**
     * @brief Get transaction hash
     * @return Hash string
     */
    std::string hash() const;

    /**
     * @brief Get token contract address
     * @return Token actor ID
     */
    TokenId token() const;

    /**
     * @brief Get transaction type
     * @return Transaction type enum
     */
    TransactionType type() const;

    /**
     * @brief Get creation timestamp
     * @return Unix timestamp
     */
    std::uint64_t timestamp() const;

    /**
     * @brief Get previous transaction hashes
     * @return Set of previous hash strings
     */
    std::set<std::string> prev_hashs() const;

    /**
     * @brief Get digital signature
     * @return Transaction signature
     */
    Signature signature() const;

    /**
     * @brief Calculate Blake3 hash of transaction
     * @return Calculated hash string
     */
    std::string calculate_hash() const;

    /**
     * @brief Update stored hash with calculated value
     */
    void update_hash();

    /**
     * @brief Check if transaction is empty
     * @return true if transaction has no meaningful data
     */
    virtual bool is_empty() const;

    /**
     * @brief Check if transaction is burn type
     * @return true if transaction burns tokens
     */
    virtual bool is_burn() const;

    /**
     * @brief Check if transaction has valid signature
     * @return true if transaction is digitally signed
     */
    bool is_signed() const;

    /**
     * @brief Less-than comparison operator for sorting
     * @param other Transaction to compare with
     * @return true if this transaction is less than other
     */
    bool operator<(const Transaction &other) const;

    /**
     * @brief Equality comparison operator
     * @param transaction Transaction to compare with
     * @return true if transactions are identical
     */
    bool operator==(const Transaction &transaction) const;

    /**
     * @brief Copy assignment operator
     * @param transaction Transaction to copy
     */
    void operator=(const Transaction &transaction);

    /**
     * @brief Move assignment operator
     * @param other Transaction to move
     * @return Reference to this transaction
     */
    Transaction &operator=(Transaction &&other) noexcept;

    /**
     * @brief Set chain section
     * @param value Section number
     */
    void set_section(const BigNumber &value);

    /**
     * @brief Set transaction type
     * @param newType New transaction type
     */
    void set_type(TransactionType newType);

    /**
     * @brief Set sender address
     * @param value Sender actor ID
     */
    void set_sender(const ActorId &value);

    /**
     * @brief Set receiver address
     * @param value Receiver actor ID
     */
    void set_receiver(const ActorId &value);

    /**
     * @brief Set token contract address
     * @param value Token actor ID
     */
    void set_token(const ActorId &value);

    /**
     * @brief Set transaction amount
     * @param value Amount as BigNumberFloat
     */
    void set_amount(const BigNumberFloat &value);

    /**
     * @brief Set creation timestamp
     * @param new_timestamp Unix timestamp
     */
    void set_timestamp(std::uint64_t new_timestamp);

    /**
     * @brief Set metadata payload
     * @param value Metadata string
     */
    void set_meta(const std::string &value);

    /**
     * @brief Set previous transaction hashes
     * @param prev_hashs Set of hash strings
     */
    void set_prev_hashs(const std::set<std::string> &prev_hashs);

    /**
     * @brief Add previous transaction hash
     * @param hash Hash string to add
     */
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

/**
 * @brief Transaction with processing operation information
 */
struct TransactionInfo {
    TransactionAmountOperation operation = TransactionAmountOperation::Plus; ///< Balance operation type
    Transaction                transaction;                                  ///< The transaction data
    std::string                hash;
};
BOOST_DESCRIBE_STRUCT(TransactionInfo, (), (operation, transaction))
