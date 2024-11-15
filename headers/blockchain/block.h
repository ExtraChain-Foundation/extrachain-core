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
#include "blockchain/transaction.h"
#include "utils/bignumber.h"
#include "utils/exc_utils.h"
#include <QDateTime>
#include <QDebug>
#include <QString>

// Block comparison result
struct Approver {
    ActorId     actorId;
    std::string sign      = "";
    bool        isApprove = false;

    std::string toString() const {
        auto               actorIdStr = actorId.to_string();
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
    bool operator==(const Approver &) const  = default;

    MSGPACK_DEFINE(actorId, sign, isApprove)
};

QDebug operator<<(QDebug debug, const Approver &approvers);

enum class BlockType {
    Data,
    Genesis,
    Dummy,
};
MSGPACK_ADD_ENUM(BlockType)
FORMAT_ENUM(BlockType)

enum class BlockError {
    Unknown,
    EmptyBlockchain,
    NoGenesis,
    NotExists,
    Invalid,
    AlreadyExists,
    AlreadyChained,

    CantMerge,
    MergeEqual
};
FORMAT_ENUM(BlockError)

enum class BlockSignError {
    NoError,
    InvalidSignature,
    NoActorSignature,
    EmptySignatures
};
FORMAT_ENUM(BlockSignError)

class BlockVariant;
using Signatures   = std::map<ActorId, Signature>;
using Transactions = std::set<Transaction>;

class EXTRACHAIN_EXPORT Block {
protected:
    BlockType             m_type;                  // simple block, or genesis block (or other)
    std::set<std::string> m_dataService;           // payload (serialized transaction's, or other)
    BigNumber             m_index = BigNumber(-1); // block id
    std::uint64_t         m_date;
    std::string           m_prevHash;     // previous block hash
    std::string           m_hash;         // this block hash (from all previous fields)
    Signatures            m_signatures;   // digital signature
    Transactions          m_transactions; // all transactions

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
        std::string           &&type,
        std::string           &&data,
        BigNumber               idx,
        std::uint64_t           date,
        std::string           &&prevHash,
        std::string           &&hash,
        Signatures            &&signatures,
        std::set<Transaction> &&transactions);

    virtual ~Block();

protected:
    /**
     * Calculates hash of this block and writes hash to "hash" variable.
     * Uses sha3.
     */
    virtual void               calcHash();
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

    // digital signature
    void           sign(const std::shared_ptr<Actor<KeyPrivate>> actor);
    BlockSignError verify(const Actor<KeyPublic> &actor) const;

    bool                equals(const Block &block) const;
    bool                isEmpty() const;
    virtual std::string toString() const;
    bool                operator<(const Block &other);
    bool                isApprover(const ActorId &) const;

public:
    void                         setPrevHash(const std::string &value);
    BlockType                    getType() const;
    std::string                  getTypeStr() const;
    BigNumber                    getIndex() const;
    std::uint64_t                getDate() const;
    const std::set<std::string> &dataService() const;
    std::string                  getDataMessagePack() const;
    std::string                  getHash() const;
    std::string                  getPrevHash() const;
    const Signatures            &signatures() const;
    const Transactions          &transactions() const;

    void addSignature(const ActorId &id, const Signature &sign, bool isApprove);
    void addSignatures(const Signatures &approvers);
    void clearSignatures();

    void         setIndex(const BigNumber &index);
    void         setDate(std::uint64_t value);
    void         setDataServiceFromMessagePack(const std::string &value);
    Block        operator=(const Block &block);
    virtual void setType(BlockType value);
    virtual void setType(const std::string &value);

    void setPrev(const BlockVariant &prev);

    void addTransaction(const Transaction &transaction);
    void addTransactions(const std::set<Transaction> &transactions);
    void addTransactions(const std::vector<Transaction> &transactions);

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string index_str = m_index.to_string();
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
        m_index = BigNumber(index_str);
    }
};

inline bool operator<(const Block &l, const Block &r) {
    eFatal("Block: incorrect operator<");
    return l.getIndex() < r.getIndex() || l.dataService() < r.dataService();
}

inline bool operator==(const Block &l, const Block &r) {
    return l.getIndex() == r.getIndex() && l.getPrevHash() == r.getPrevHash()
           && l.dataService() == r.dataService() && l.transactions() == r.transactions();
}

QDebug operator<<(QDebug debug, const Block &block);

#endif // MEMBLOCK_H
