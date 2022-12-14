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
#include "utils/bignumber_float.h"
#include "utils/dfs_utils.h"
#include "datastorage/transaction.h"
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

class EXTRACHAIN_EXPORT RewardTransaction : public Transaction {
    ActorId senderReceiver;
    TransactionRewardData rewardData; // reward additional payload field
    BigNumber amountReward;

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

    RewardTransaction(const Transaction &transaction);


    void setSenderReceiverBalance(BigNumber balance);
    void clear();
    ActorId getSenderReceiver() const { return senderReceiver; }
    TransactionRewardData getRewardData() const { return rewardData; }
    virtual bool isEmpty() const override;
    bool operator==(const RewardTransaction &transaction) const;
    bool operator!=(const RewardTransaction &transaction) const;
    void operator=(const RewardTransaction &transaction);
    std::string serialize() const;
    bool deserialize(const QByteArray &serialized);
    QString toString() const;
    void setRewardData(const TransactionRewardData &transactionRewardData);
    void setSenderReceiver(const ActorId &value);
    void insertAdditionalData(AdditionalData& additionalData);

    Transaction convertToTransaction();
    void setAmountReward(const BigNumber& value);

    BigNumber getAmountReward() const;

    MSGPACK_DEFINE(senderReceiver, amount, date, rewardData, token, prevBlock, gas, hop, hash, approver,
                   producer, digSig, amountReward, typeTx)
protected:
    void calcAmount();
};

#endif // REWARD_TRANSACTION_H
