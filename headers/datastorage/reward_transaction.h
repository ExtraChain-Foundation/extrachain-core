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

#ifndef REWARD_TRANSACTION_H
#define REWARD_TRANSACTION_H

#include "datastorage/actor.h"
#include "utils/bignumber.h"
#include "utils/dfs_utils.h"
#include "utils/exc_utils.h"
#include <QByteArray>
#include <QDateTime>
#include <QString>

struct RecieveData {
    std::string actor;
    std::string fileName;
    std::string fileHash;
    std::string fragmentHash;

    MSGPACK_DEFINE(actor, fileName, fileHash, fragmentHash)
};

struct AdditionalData {
    std::vector<DFSP::VerifyFileMessage> verifiedFragments;
    ActorId actorVerifier;
    std::string hashRecord;
    int calcReward() const;

    MSGPACK_DEFINE(verifiedFragments, actorVerifier, hashRecord)
};

struct TransactionRewardData {
    TransactionRewardData() {
    }
    TransactionRewardData(const std::vector<RecieveData> &recieveDataList)
        : recieveDataList(recieveDataList) {
    }
    std::vector<AdditionalData> additionalData;
    bool isEmpty() const;
    std::vector<RecieveData> getRecieveDataList() {
        return recieveDataList;
    }
    MSGPACK_DEFINE(recieveDataList, additionalData);
private:
    std::vector<RecieveData> recieveDataList;
};

class EXTRACHAIN_EXPORT RewardTransaction {
    /**
     * Calculates hash of this block and writes hash to "hash" variable.
     * Uses sha3.
     */
    void calcHash();

    ActorId senderReceiver;
    BigNumber amount; // coin amount
    long long date;
    TransactionRewardData rewardData; // reward additional payload field
    ActorId token;                    // token contract address
    BigNumber prevBlock;              // last block id at the moment of tx creation
    int gas;                          // security and reward param
    int hop;                          // number of the nodes, through which the transaction will pass before
                                      // aprovement
    std::string hash;                 // hash from all fields
    ActorId approver;                 // address of the transaction approver.
    ActorId producer;
    std::string digSig;

public:
    // Construct empty transaction
    RewardTransaction();

    // Deserialize already created transaction
    RewardTransaction(const QByteArray &serialized);

    // Construct transaction
    RewardTransaction(const ActorId &senderReceiver);

    RewardTransaction(const ActorId &senderReceiver, const TransactionRewardData &rewardData);


    // Construct transaction with data
    RewardTransaction(const ActorId &senderReceiver, const QByteArray &data,
                      const TransactionRewardData &rewardData);

    RewardTransaction(const RewardTransaction &other);

    /**
     * @brief Concatenates all fields that are used for digSig calculation
     * Override in subclasses
     * @return digSig data
     */
    QByteArray getDataForHash() const;
    QByteArray getDataForDigSig() const;

    // digital signature
    void sign(const Actor<KeyPrivate> &actor);
    bool verify(const Actor<KeyPublic> &actor) const;

    void setSenderReceiverBalance(BigNumber balance);
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
    ActorId getSenderReceiver() const;
    BigNumber getAmount() const;
    BigNumber getPrevBlock() const;
    TransactionRewardData getRewardData() const;
    QByteArray getHash() const;
    ActorId getToken() const;
    ActorId getApprover() const;
    QByteArray getDigSig() const;
    ActorId getProducer() const;

    bool isEmpty() const;
    bool operator==(const RewardTransaction &transaction) const;
    bool operator!=(const RewardTransaction &transaction) const;
    void operator=(const RewardTransaction &transaction);

    std::string serialize() const;
    bool deserialize(const QByteArray &serialized);
    QString toString() const;
    long long getDate() const;
    void setDate(long long value);
    void setToken(const ActorId &value);
    void setRewardData(const TransactionRewardData &transactionRewardData);
    /**
     * @brief 1.1 -> 1.1 * 10e18 in BigNumber
     * @param amount
     */
    static BigNumber visibleToAmount(QByteArray amount);

    /**
     * @brief 1 * 10e18 from BigNumber to number -> 1
     * @param number
     */
    static QString amountToVisible(const BigNumber &number);
    static BigNumber amountNormalizeMul(const BigNumber &number);
    static BigNumber amountMul(const BigNumber &number1, const BigNumber &number2);
    static BigNumber amountDiv(const BigNumber &number1, const BigNumber &number2);
    static BigNumber amountPercent(BigNumber number, uint percent);
    void setAmount(const BigNumber &value);
    void setSenderReceiver(const ActorId &value);
    void insertAdditionalData(AdditionalData& additionalData);

    MSGPACK_DEFINE(senderReceiver, amount, date, rewardData, token, prevBlock, gas, hop, hash, approver,
                   producer, digSig)
protected:
    void calcAmount();
};

#endif // REWARD_TRANSACTION_H
