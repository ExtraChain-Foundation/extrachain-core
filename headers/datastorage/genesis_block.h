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
    BigNumberFloat state;
    ActorId token;
    DataStorage::typeDataRow type;

public:
    GenesisDataRow() = default;

    explicit GenesisDataRow(const ActorId &actorId, const BigNumberFloat &state, const ActorId &token,
                            const DataStorage::typeDataRow &type)
        : actorId(actorId)
        , state(state)
        , token(token)
        , type(type) {
    }

    explicit GenesisDataRow(const std::string &serialized) {
        deserialize(serialized);
    }

    std::string serialize() const {
        std::vector<std::string> l;

        l.push_back(actorId.toStdString());
        l.push_back(state.toStdString());
        l.push_back(token.toStdString());
        l.push_back(std::to_string(type));
        return Serialization::serialize(l);
    }

    void deserialize(const std::string &serialized) {
        std::vector<std::string> l = Serialization::deserialize(serialized);
        if (l.size() == 4) {
            actorId = l.at(0);
            state = BigNumber(l.at(1));
            token = l.at(2);
            type = DataStorage::typeDataRow(std::stoi(l.at(3)));
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
    std::string getDataForHash() const override;          // deprecate?
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
        index = index_str;
    }
};

#endif // GENESIS_BLOCK_H
