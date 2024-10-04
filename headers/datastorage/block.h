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
#include "utils/exc_utils.h"
#include <QDateTime>
#include <QDebug>
#include <QString>

// Block comparison result
struct Approver {
    ActorId actorId;
    std::string sign = "";
    bool isApprove = false;

    std::string toStdString() const {
        auto actorIdStr = actorId.toStdString();
        std::ostringstream oss;
        oss << "Approver { "
            << "actor_id: \""
            << actorIdStr.substr(0, 5) + "..."
                   + actorIdStr.substr(actorIdStr.size() - 5, actorIdStr.size() - 1)
            << "\", "
            << "sign: \"" << (sign.empty() ? "" : sign.substr(0, 8) + "...") << "\", "
            << "is_approve: " << std::boolalpha << isApprove << " }";
        return oss.str();
    }

    auto operator<=>(const Approver &) const = default;
    bool operator==(const Approver &) const = default;

    MSGPACK_DEFINE(actorId, sign, isApprove)
};

QDebug operator<<(QDebug debug, const Approver &approvers);

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
    std::set<std::string> m_dataService;      // payload (serialized transaction's, or other)
    BigNumber m_index = BigNumber(-1); // block id
    long long m_date;
    std::string m_prevHash;               // previous block hash
    std::string m_hash;                   // this block hash (from all previous fields)
    std::set<Approver> m_signatures;      // digital signature
    std::set<Transaction> m_transactions; // all transactions

public:
    Block();
    /**
     * @brief Block
     * @param block
     */
    Block(const Block &block);

    /**
     * @brief Block
     */
    Block(
        std::string &&type,
        std::string &&data,
        BigNumber idx,
        long long date,
        std::string &&prevHash,
        std::string &&hash,
        std::set<Approver> &&signatures,
        std::set<Transaction> &&transactions);

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
    void addDatas(const std::set<std::string> &datas);
    void addDatas(const std::vector<std::string> &datas);

    Transaction getTransactionByHash(std::string hash) const;

    bool contain(Block &from) const;

    // digital signature
    virtual void sign(const std::shared_ptr<Actor<KeyPrivate>> actor) final;
    virtual bool verify(const Actor<KeyPublic> &actor) const final;

    bool equals(const Block &block) const;
    bool isEmpty() const;
    QString toString() const;
    virtual std::string toStdString() const;
    bool operator<(const Block &other);
    bool isApprover(const ActorId &) const;

public:
    void setPrevHash(const std::string &value);
    BlockType getType() const;
    std::string getTypeStr() const;
    BigNumber getIndex() const;
    long long getDate() const;
    const std::set<std::string> &dataService() const;
    std::string getDataMessagePack() const;
    std::string getHash() const;
    std::string getPrevHash() const;
    std::string getSignature() const;
    const std::set<Approver> &signatures() const;
    const std::set<Transaction> &transactions() const;

    void addSignature(const std::string &id, const std::string &sign, bool isApprove);
    void addSignatures(const std::set<Approver> &approvers);
    void clearSignatures();

    void setIndex(const BigNumber &index);
    void setDate(long long value);
    void setDataServiceFromMessagePack(const std::string &value);
    Block operator=(const Block &block);
    virtual void setType(BlockType value);
    virtual void setType(const std::string &value);

    void setPrev(const BlockVariant &prev);

    void addTransaction(const Transaction &transaction);
    void addTransactions(const std::set<Transaction> &transactions);
    void addTransactions(const std::vector<Transaction> &transactions);

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string index_str = m_index.toStdString();
        msgpack::type::make_define_array(
            m_type,
            index_str,
            m_date,
            m_dataService,
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
            m_dataService,
            m_hash,
            m_prevHash,
            m_signatures,
            m_transactions)
            .msgpack_unpack(msgpack_o);
        m_index = index_str;
    }
};

inline bool operator<(const Block &l, const Block &r) {
    qFatal("Block: incorrect operator<");
    return l.getIndex() < r.getIndex() || l.dataService() < r.dataService();
}

inline bool operator==(const Block &l, const Block &r) {
    return l.getIndex() == r.getIndex() && l.getPrevHash() == r.getPrevHash() && l.dataService() == r.dataService()
           && l.transactions() == r.transactions();
}

QDebug operator<<(QDebug debug, const Block &block);

#endif // MEMBLOCK_H
