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

namespace Config {
static const std::string DATA_BLOCK_TYPE = "data";
static const std::string MERGE_BLOCK = "dataMerge";
}

class EXTRACHAIN_EXPORT Block {
protected:
    const int FIELDS_SIZE = 4;
    std::string m_type = Config::DATA_BLOCK_TYPE; // simple block, or genesis block (or other)
    std::string data;                             // payload (serialized tx's, or other)
    BigNumber index = BigNumber(-1);              // block id
    //    BigNumber approver = BigNumber(-1);        // block approver id

    long long date;
    std::string prevHash; // previous block hash
    std::string hash;     // this block hash (from all previous fields)
    //    QByteArray digSig;   // digital signature (from all fields)
    std::vector<Approvers> signatures;

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
     * Uses sha3.
     */
    void calcHash();

protected:
    /**
     * @brief Concatenates all fields that are used for digSig calculation
     * Override in subclasses
     * @return digSig data
     */
    virtual QByteArray getDataForHash() const;
    virtual const std::string &getDataForDigSig() const;

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
    std::vector<Transaction> extractTransactions() const;
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
    bool isApprover(const ActorId &) const;

public:
    virtual void initFields(QList<QByteArray> &list);
    QList<Block> getDataFromAllBlocks(QList<QByteArray>);
    void setPrevHash(const std::string &value);
    std::string getType() const;
    ActorId getApprover() const;
    BigNumber getIndex() const;
    std::string getData() const;
    std::string getHash() const;
    std::string getPrevHash() const;
    std::string getDigSig() const;
    QByteArrayList getListSignatures() const;
    void addSignature(const QByteArray &id, const QByteArray &sign, const bool &isApprover);
    // void setType(QByteArray type);
    long long getDate() const;
    void setDate(long long value);
    Block operator=(const Block &block);
    void setType(const std::string &value);

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string index_str = index.toStdString();
        msgpack::type::make_define_array(m_type, index_str, date, data, hash, prevHash, signatures)
            .msgpack_pack(msgpack_pk);
    }

    void msgpack_unpack(msgpack::object const &msgpack_o) {
        std::string index_str;
        msgpack::type::make_define_array(m_type, index_str, date, data, hash, prevHash, signatures)
            .msgpack_unpack(msgpack_o);
        index = index_str;
    }
};

inline bool operator<(const Block &l, const Block &r) {
    return l.getIndex() < r.getIndex() || l.getData() < r.getData();
}

inline bool operator==(const Block &l, const Block &r) {
    return l.getIndex() == r.getIndex() && l.getPrevHash() == r.getPrevHash()
        && l.extractTransactions() == r.extractTransactions();
}

#endif // MEMBLOCK_H
