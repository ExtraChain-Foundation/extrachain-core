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

#ifndef GENESIS_BLOCK_H
#define GENESIS_BLOCK_H

#include "datastorage/block.h"

/**
 * @brief Representation of one row in genesis block data field
 */

class EXTRACHAIN_EXPORT GenesisDataRow {
public:
    ActorId actorId;
    BigNumber state;
    ActorId token;
    DataStorage::typeDataRow type;

public:
    GenesisDataRow() = default;

    explicit GenesisDataRow(const ActorId &actorId, const BigNumber &state, const ActorId &token,
                            const DataStorage::typeDataRow &type)
        : actorId(actorId)
        , state(state)
        , token(token)
        , type(type) {
    }

    explicit GenesisDataRow(const QByteArray &serialized) {
        deserialize(serialized);
    }

    QByteArray serialize() const {
        QList<QByteArray> l;
        l << actorId.toByteArray() << state.toByteArray() << token.toByteArray() << QByteArray::number(type);
        return Serialization::serialize(l, Serialization::DEFAULT_FIELD_SIZE);
    }

    void deserialize(const QByteArray &serialized) {
        QList<QByteArray> l = Serialization::deserialize(serialized, Serialization::DEFAULT_FIELD_SIZE);
        if (l.size() == 4) {
            actorId = l.at(0).toStdString();
            state = BigNumber(l.at(1));
            token = l.at(2).toStdString();
            type = DataStorage::typeDataRow(l.at(3).toInt());
        }
    }

    bool operator==(const GenesisDataRow &other) const {
        return this->actorId == other.actorId && this->state == other.state && this->token == other.token
            && this->type == other.type;
    }
};

namespace Config {
static const std::string GENESIS_BLOCK_TYPE = "genesis";
static const std::string GENESIS_BLOCK_MERGE = "genesisMerge";
}

/**
 * @brief Genesis block it's an extended block, with has specific data field
 * and one additional field - prevGenHash.
 */
class EXTRACHAIN_EXPORT GenesisBlock : public Block {
public:
    std::string prevGenHash; // previous genesis block hashes

public:
    GenesisBlock();
    GenesisBlock(const GenesisBlock &block);

    // Deserialize already constructed block
    explicit GenesisBlock(const QByteArray &serialized);

    // Initial block construction, prev = nullptr for first block
    explicit GenesisBlock(const QByteArray &_data, const Block &prevBlock, const QByteArray &prevGenHash);

    // Block interface
public:
    void addRow(const GenesisDataRow &row);
    QByteArray getDataForHash() const override;           // deprecate?
    const std::string &getDataForDigSig() const override; // deprecate?
    bool deserialize(const QByteArray &serialized) override;
    QByteArray serialize() const override;
    void initFields(QList<QByteArray> &list) override;

    /**
     * @brief extract non-empty genesisDataRows from data
     * @return genesis data row list
     */
    QList<GenesisDataRow> extractDataRows() const;
    static bool isGenesisBlock(const QByteArray &serialized);

public:
    std::string getPrevGenHash() const;
    void setPrevGenHash(const std::string &value);

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string index_str = index.toStdString();
        msgpack::type::make_define_array(m_type, index_str, date, data, hash, prevHash, signatures,
                                         prevGenHash)
            .msgpack_pack(msgpack_pk);
    }
    void msgpack_unpack(msgpack::object const &msgpack_o) {
        std::string index_str;
        msgpack::type::make_define_array(m_type, index_str, date, data, hash, prevHash, signatures,
                                         prevGenHash)
            .msgpack_unpack(msgpack_o);
        index = QByteArray::fromStdString(index_str);
    }
};

#endif // GENESIS_BLOCK_H
