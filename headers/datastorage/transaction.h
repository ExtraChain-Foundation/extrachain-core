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
#include <QDateTime>
#include <QString>
enum class TypeTx {
    Transaction = 0,
    RewardTransaction = 1
};
MSGPACK_ADD_ENUM(TypeTx)
FORMAT_ENUM(TypeTx)

struct TransactionData {
    std::string hash;
    std::string path;
    MSGPACK_DEFINE(hash, path)
};

class EXTRACHAIN_EXPORT Transaction {
    ActorId sender;
    ActorId receiver;

protected:
    /**
     * Calculates hash of this block and writes hash to "hash" variable.
     * Uses sha3.
     */
    void calcHash();
    BigNumberFloat amount; // coin amount
    long long date;
    std::string data;    // additional payload field
    ActorId token;       // token contract address
    BigNumber prevBlock; // last block id at the moment of tx creation
    int gas;             // security and reward param
    int hop;             // number of the nodes, through which the transaction will pass before
                         // aprovement
    std::string hash;    // hash from all fields
    ActorId approver;    // address of the transaction approver.
    ActorId producer;
    std::string digSig;
    TypeTx typeTx = TypeTx::Transaction;

public:
    // Construct empty transaction
    Transaction();
    // Deserialize already created transaction
    Transaction(const std::string &serialized);

    // Construct transaction
    Transaction(const ActorId &sender, const ActorId &receiver, const BigNumberFloat &amount);

    // Construct transaction with data
    Transaction(const ActorId &sender, const ActorId &receiver, const BigNumberFloat &amount,
                const std::string &data);

    Transaction(const Transaction &other);

    /**
     * @brief Concatenates all fields that are used for digSig calculation
     * Override in subclasses
     * @return digSig data
     */
    std::string getDataForHash() const;
    std::string getDataForDigSig() const;

    // digital signature
    void sign(const Actor<KeyPrivate> &actor);
    bool verify(const Actor<KeyPublic> &actor) const;

    //    void setSenderBalance(BigNumber balance);
    //    void setReceiverBalance(BigNumber balance);
    void setPrevBlock(const BigNumber &value);
    void setGas(int gas);
    void setHop(int hop);
    void setProducer(const ActorId &value);
    void setDigSig(const std::string &value);
    void setApprover(const ActorId &value);
    void setHash(const std::string &value);
    void decrementHop();
    void clear();

    int getGas() const;
    int getHop() const;
    ActorId getSender() const;
    ActorId getReceiver() const;
    BigNumberFloat getAmount() const;
    BigNumber getPrevBlock() const;
    std::string getData() const;
    std::string getHash() const;
    ActorId getToken() const;
    ActorId getApprover() const;
    std::string getDigSig() const;
    ActorId getProducer() const;

    virtual bool isEmpty() const;
    bool operator==(const Transaction &transaction) const;
    bool operator!=(const Transaction &transaction) const;
    void operator=(const Transaction &transaction);

    std::string serialize() const;
    bool deserialize(const std::string &serialized);
    QString toString() const;
    long long getDate() const;
    void setDate(long long value);
    void setToken(const ActorId &value);
    void setData(const std::string &value);
    /**
     * @brief 1.1 -> 1.1 * 10e18 in BigNumber
     * @param amount
     */
    static BigNumberFloat visibleToAmount(std::string amount);

    /**
     * @brief 1 * 10e18 from BigNumber to number -> 1
     * @param number
     */
    static QString amountToVisible(const BigNumberFloat &number);
    static BigNumberFloat amountNormalizeMul(const BigNumberFloat &number);
    static BigNumberFloat amountMul(const BigNumberFloat &number1, const BigNumberFloat &number2);
    static BigNumberFloat amountDiv(const BigNumberFloat &number1, const BigNumberFloat &number2);
    static BigNumberFloat amountPercent(BigNumberFloat number, uint percent);
    void setAmount(const BigNumberFloat &value);
    void setSender(const ActorId &value);
    void setReceiver(const ActorId &value);
    bool isRewardTransaction() const;
    TypeTx getTypeTx() const;
    virtual void setTypeTx(TypeTx newTypeTx);

    MSGPACK_DEFINE(sender, receiver, amount, date, data, token, prevBlock, gas, hop, hash, approver, producer,
                   digSig, typeTx)
};

#endif // TRANSACTION_H
