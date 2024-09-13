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

#include "actor.h"
#include "datastorage/transaction.h"
#include "utils/bignumber.h"
#include "utils/db_connector.h"
#include "utils/exc_utils.h"
#include <QDateTime>
#include <QDebug>
#include <QString>

// Block comparison result
struct Approvers {
    std::string actorId = "";
    std::string sign = "";
    bool isApprove = false;

    MSGPACK_DEFINE(actorId, sign, isApprove)
};

struct BlockCompare {
    BigNumber indexDiff;
    BigNumber approverDiff;
    int dataDiff;
    int prevHashDiff;
    int hashDiff;
    int digitalSigDiff;
};

enum class BlockType {
    Data,
    Genesis,
    DataMerge,
    GenesisMerge,
    Dummy,
};
MSGPACK_ADD_ENUM(BlockType)
FORMAT_ENUM(BlockType)

class BlockVariant;

class EXTRACHAIN_EXPORT Block {
protected:
    BlockType m_type;                  // simple block, or genesis block (or other)
    std::string m_data;                // payload (serialized tx's, or other)
    BigNumber m_index = BigNumber(-1); // block id
    // BigNumber approver = BigNumber(-1);        // block approver id

    long long m_date;
    std::string m_prevHash; // previous block hash
    std::string m_hash;     // this block hash (from all previous fields)
    // QByteArray signature;   // digital signature (from all fields)
    std::vector<Approvers> m_signatures;     // digital signature
    std::vector<Transaction> m_transactions; // all transactions

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
    Block(const std::string &serialized);
    /**
     * @brief Block
     * Initial block construction, prev = nullptr for first block
     * @param data
     * @param prev
     */
    Block(const std::string &data, const BlockVariant &prev);

    Block(
        std::string &&type,
        std::string &&data,
        BigNumber idx,
        long long date,
        std::string &&prevHash,
        std::string &&hash,
        std::vector<Approvers> &&signatures,
        std::vector<Transaction> &&transactions);

    virtual ~Block();

protected:
    /**
     * Calculates hash of this block and writes hash to "hash" variable.
     * Uses sha3.
     */
    virtual void calcHash();
    virtual const std::string &getDataForSignature() const;

public:
    // data operations

    /**
     * @brief add data to this block
     * @param data
     */
    void addData(const std::string &data);

    void setData(const std::string &data);

    void initializeData(const std::string &serializedData);
    /**
     * @brief extract non-empty transactions from data
     * @return transaction list
     */
    [[deprecated("Use transactions().")]]
    std::vector<Transaction> extractTransactions() const;
    Transaction getTransactionByHash(std::string hash) const;

    bool contain(Block &from) const;

    // digital signature
    virtual void sign(const Actor<KeyPrivate> &actor) final;
    virtual bool verify(const Actor<KeyPublic> &actor) const final;

    // serialization

    virtual std::string serialize() const;
    virtual bool deserialize(const std::string &serialized);

    bool equals(const Block &block) const;
    BlockCompare compareBlock(const Block &b) const;
    bool isEmpty() const;
    QString toString() const;
    bool operator<(const Block &other);
    bool isApprover(const ActorId &) const;

public:
    void setPrevHash(const std::string &value);
    BlockType getType() const;
    std::string getTypeStr() const;
    ActorId getApprover() const;
    BigNumber getIndex() const;
    std::string getData() const;
    std::string getHash() const;
    std::string getPrevHash() const;
    std::string getSignature() const;
    const std::vector<Approvers> &signatures() const;
    const std::vector<Transaction> &transactions() const;

    // TODO: replace to signatures()
    QByteArrayList getListSignatures() const;
    void addSignature(const QByteArray &id, const QByteArray &sign, const bool &isApprover);
    // void setType(QByteArray type);
    long long getDate() const;
    void setDate(long long value);
    Block operator=(const Block &block);
    virtual void setType(BlockType value);
    virtual void setType(const std::string &value);

    void addTransaction(const Transaction &transaction);
    void addTransactions(const std::vector<Transaction> &transactions);

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string index_str = m_index.toStdString();
        msgpack::type::make_define_array(
            m_type,
            index_str,
            m_date,
            m_data,
            m_hash,
            m_prevHash,
            m_signatures,
            m_transactions)
            .msgpack_pack(msgpack_pk);
    }

    void msgpack_unpack(msgpack::object const &msgpack_o) {
        std::string index_str;
        msgpack::type::make_define_array(
            m_type,
            index_str,
            m_date,
            m_data,
            m_hash,
            m_prevHash,
            m_signatures,
            m_transactions)
            .msgpack_unpack(msgpack_o);
        m_index = index_str;
    }
};

inline bool operator<(const Block &l, const Block &r) {
    return l.getIndex() < r.getIndex() || l.getData() < r.getData();
}

inline bool operator==(const Block &l, const Block &r) {
    return l.getIndex() == r.getIndex() && l.getPrevHash() == r.getPrevHash()
           && l.transactions() == r.transactions();
}

#endif // MEMBLOCK_H
