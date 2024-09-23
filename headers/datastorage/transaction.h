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

#ifndef TRANSACTION_H
#define TRANSACTION_H

#include "datastorage/actor.h"
#include "utils/bignumber.h"
#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

enum class TypeTx {
    Transaction = 0,
    RewardTransaction = 1,
    FarmingTransaction = 2,
    FarmingLockedTransaction = 3
};
MSGPACK_ADD_ENUM(TypeTx)
FORMAT_ENUM(TypeTx)

struct TransactionData {
    std::string hash;
    std::string path;
    MSGPACK_DEFINE(hash, path)
};

enum class TransactionError {
    Unknown,
    EmptyTransaction,
    NoLastBlock,
    InsufficientFunds,
    NoCurrentUser,
    ZeroAmount
};
// MSGPACK_ADD_ENUM(TransactionError)
FORMAT_ENUM(TransactionError)

class EXTRACHAIN_EXPORT Transaction {
    ActorId sender;
    ActorId receiver;

protected:
    BigNumberFloat amount; // coin amount
    long long date;
    std::string data;    // additional payload field
    ActorId token;       // token contract address
    BigNumber prevBlock; // last block id at the moment of tx creation
    std::string hash;    // hash from all fields
    ActorId approver;    // address of the transaction approver.
    ActorId producer;
    std::string signature;
    TypeTx typeTx = TypeTx::Transaction;

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
        const ActorId &sender,
        const ActorId &receiver,
        const BigNumberFloat &amount,
        const ActorId &token = ActorId(),
        const std::string &data = std::string());

    Transaction(const Transaction &other);

    // digital signature
    void sign(const std::shared_ptr<Actor<KeyPrivate>> actor);
    bool verify(const Actor<KeyPublic> &actor) const;

    // void setSenderBalance(BigNumber balance);
    // void setReceiverBalance(BigNumber balance);
    void setPrevBlock(const BigNumber &value);
    void setProducer(const ActorId &value);
    void setSignature(const std::string &value);
    void setApprover(const ActorId &value);
    void setHash(const std::string &value);

    ActorId getSender() const;
    ActorId getReceiver() const;
    BigNumberFloat getAmount() const;
    std::string getAmountDec() const;
    BigNumber getPrevBlock() const;
    std::string getData() const;
    std::string getHash() const;
    ActorId getToken() const;
    ActorId getApprover() const;
    std::string getSignature() const;
    ActorId getProducer() const;

    virtual bool isEmpty() const;
    virtual bool isBurn() const;
    auto operator<=>(const Transaction &) const = default;
    bool operator==(const Transaction &transaction) const;
    void operator=(const Transaction &transaction);

    std::string toStdString() const;
    QString toString() const;
    long long getDate() const;
    void setDate(long long value);
    void setToken(const ActorId &value);
    void setData(const std::string &value);
    void setAmount(const BigNumberFloat &value);
    void setSender(const ActorId &value);
    void setReceiver(const ActorId &value);
    bool isRewardTransaction() const;
    bool isFarmingTransaction() const;
    bool isLockedFarmingTransaction() const;
    TypeTx getTypeTx() const;
    virtual void setTypeTx(TypeTx newTypeTx);

    MSGPACK_DEFINE(
        sender,
        receiver,
        amount,
        date,
        data,
        token,
        prevBlock,
        hash,
        approver,
        producer,
        signature,
        typeTx)
};

struct FarmingTransactionData {
    BigNumber index;
    Transaction transaction;

    void decrementIndex() {
        index -= BigNumber(1);
    }

    bool canImproveTx() {
        return index <= 0;
    }
};

QDebug operator<<(QDebug debug, const Transaction &tx);

#endif // TRANSACTION_H
