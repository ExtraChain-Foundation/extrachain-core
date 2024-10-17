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
 * @file genesis_block.h
 * @brief Defines structures for genesis block
 */

/**
 * @brief Representation of rows in genesis block data field
 */
struct EXTRACHAIN_EXPORT GenesisDataActor {
    ActorId actorId; /**< Unique identifier for the actor */
    TokenId tokenId; /**< Identifier for the associated token */

    auto operator<=>(const GenesisDataActor &) const = default;
    bool operator==(const GenesisDataActor &) const  = default;

    /**
     * @brief MessagePack serialization definition
     */
    MSGPACK_DEFINE(actorId, tokenId)
};

/**
 * @brief Information associated with genesis data
 */
struct EXTRACHAIN_EXPORT GenesisDataInfo {
    BigNumberFloat           state = 0; /**< State represented as a big number float, default is 0 */
    DataStorage::DataRowType type =
        DataStorage::DataRowType::Universal; /**< Type of the data row, default is Universal */

    auto operator<=>(const GenesisDataInfo &) const = default;
    bool operator==(const GenesisDataInfo &) const  = default;

    /**
     * @brief MessagePack serialization definition
     */
    MSGPACK_DEFINE(state, type)
};

/**
 * @typedef GenesisDataRows
 * @brief A map of GenesisDataActor to GenesisDataInfo, representing rows of genesis data
 */
using GenesisDataRows = std::map<GenesisDataActor, GenesisDataInfo>;

/**
 * @brief Genesis block it's an extended block, with has specific data field
 * and one additional field - prevGenHash.
 */
class EXTRACHAIN_EXPORT GenesisBlock : public Block {
private:
    std::string     m_prevGenHash; // previous genesis block hashes
    GenesisDataRows m_dataRows;

public:
    GenesisBlock();
    GenesisBlock(const GenesisBlock &block);

    GenesisBlock(
        std::string     &&type,
        std::string     &&data,
        BigNumber         idx,
        long long         date,
        std::string     &&prevHash,
        std::string     &&hash,
        std::string     &&prevGenHash,
        Signatures      &&signatures,
        GenesisDataRows &&dataRows);

    // Block interface
public:
    void addRow(const ActorId &actorId, const TokenId &tokenId, const GenesisDataInfo &row);
    void addRows(const GenesisDataRows &dataRows);

    const std::string &getDataForSignature() const override;

    /**
     * @brief extract non-empty GenesisDataInfos from data
     * @return genesis data row list
     */
    const GenesisDataRows &dataRows() const;
    std::string            toStdString() const override;

protected:
    void calcHash() override;

public:
    std::string getPrevGenHash() const;
    void        setPrevGen(const BlockVariant &block);
    void        setType(BlockType value) override;
    void        setType(const std::string &value) override;

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
            m_prevGenHash,
            m_dataRows)
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
            m_prevGenHash,
            m_dataRows)
            .msgpack_unpack(msgpack_o);
        m_index = index_str;
    }
};

inline bool operator==(const GenesisBlock &l, const GenesisBlock &r) {
    return l.getIndex() == r.getIndex() && l.getPrevHash() == r.getPrevHash()
           && l.dataService() == r.dataService() && l.dataRows() == r.dataRows();
}

#endif // GENESIS_BLOCK_H
