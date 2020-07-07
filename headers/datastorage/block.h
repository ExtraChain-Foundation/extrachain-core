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

#ifndef MEMBLOCK_H
#define MEMBLOCK_H

#include <QString>
#include <QDebug>
#include "enc/sign_interface.h"
#include "utils/bignumber.h"
#include "utils/utils.h"
#include "datastorage/transaction.h"
#include "actor.h"
#include "utils/utils.h"
#include <QDateTime>
#include "headers/utils/db_connector.h"

// Block comparison result
struct forApprovers
{
    QByteArray actorId = "";
    QByteArray sign = "";
    bool isApprove = false;
};
struct BlockCompare
{
    BigNumber indexDiff;
    BigNumber approverDiff;
    int dataDiff;
    int prevHashDiff;
    int hashDiff;
    int digitalSigDiff;
};

namespace Config {
static const QByteArray DATA_BLOCK_TYPE = "data";
static const QByteArray MERGE_BLOCK = "dataMerge";
}

class Block
{

protected:
    const int FIELDS_SIZE = 4;
    QByteArray type = Config::DATA_BLOCK_TYPE; // simple block, or genesis block (or other)
    QByteArray data;                           // payload (serialized tx's, or other)
    BigNumber index = BigNumber(-1);           // block id
    //    BigNumber approver = BigNumber(-1);        // block approver id

    long long date;
    QByteArray prevHash; // previous block hash
    QByteArray hash;     // this block hash (from all previous fields)
    //    QByteArray digSig;   // digital signature (from all fields)
    QList<forApprovers> signatures;

public:
    Block();
    /**
     * @brief Block
     * @param block
     */
    Block(const Block &block);
    /**
     * @brief Block
     * Deserialize already constructed block
     * @param serialized
     */
    Block(const QByteArray &serialized);
    /**
     * @brief Block
     * Initial block construction, prev = nullptr for first block
     * @param data
     * @param prev
     */
    Block(const QByteArray &data, const Block &prev);

    virtual ~Block();

private:
    /**
     * Calculates hash of this block and writes hash to "hash" variable.
     * Uses keccak.
     */
    void calcHash();

protected:
    /**
     * @brief Concatenates all fields that are used for digSig calculation
     * Override in subclasses
     * @return digSig data
     */
    virtual QByteArray getDataForHash() const;
    virtual QByteArray getDataForDigSig() const;

public:
    // data operations

    /**
     * @brief add data to this block
     * @param data
     */
    void addData(const QByteArray &data);
    /**
     * @brief extract non-empty transactions from data
     * @return transaction list
     */
    QList<Transaction> extractTransactions() const;
    Transaction getTransactionByHash(QByteArray hash) const;

    bool contain(Block &from) const;

    // digital signature
    virtual void sign(const Actor<KeyPrivate> &actor) final;
    virtual bool verify(const Actor<KeyPublic> &actor) const final;

    // serialization

    virtual QByteArray serialize() const;
    virtual bool deserialize(const QByteArray &serialized);

    bool equals(const Block &block) const;
    BlockCompare compareBlock(const Block &b) const;
    bool isEmpty() const;
    QString toString() const;
    bool operator<(const Block &other);
    static bool isBlock(const QByteArray &data);
    bool isApprover(QByteArray) const;

public:
    virtual void initFields(QList<QByteArray> &list);
    QList<Block> getDataFromAllBlocks(QList<QByteArray>);
    void setPrevHash(const QByteArray &value);
    QByteArray getType() const;
    BigNumber getApprover() const;
    BigNumber getIndex() const;
    QByteArray getData() const;
    QByteArray getHash() const;
    QByteArray getPrevHash() const;
    QByteArray getDigSig() const;
    QByteArray getSignatures() const;
    QByteArrayList getListSignatures() const;
    void addSignature(const QByteArray &id, const QByteArray &sign, const bool &isApprover);
    // void setType(QByteArray type);
    long long getDate() const;
    void setDate(long long value);
    Block operator=(const Block &block);

    void setType(const QByteArray &value);
};

inline bool operator<(const Block &l, const Block &r)
{
    return l.getIndex() < r.getIndex() || l.getData() < r.getData();
}

inline bool operator==(const Block &l, const Block &r)
{
    return l.getIndex() == r.getIndex() && l.getPrevHash() == r.getPrevHash()
        && l.extractTransactions() == r.extractTransactions();
}

#endif // MEMBLOCK_H
