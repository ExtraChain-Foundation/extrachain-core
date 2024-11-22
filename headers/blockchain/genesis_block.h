/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once

#include "blockchain/block.h"

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
    BOOST_DESCRIBE_CLASS(GenesisDataActor, (), (), (), (actorId, tokenId))
};

/**
 * @brief Information associated with genesis data
 */
struct EXTRACHAIN_EXPORT GenesisDataInfo {
    BigNumberFloat state = BigNumberFloat(0); /**< State represented as a big number float, default is 0 */
    BlockchainConst::DataRowType type =
        BlockchainConst::DataRowType::Universal; /**< Type of the data row, default is Universal */

    auto operator<=>(const GenesisDataInfo &) const = default;
    bool operator==(const GenesisDataInfo &) const  = default;

    /**
     * @brief MessagePack serialization definition
     */
    MSGPACK_DEFINE(state, type)
    BOOST_DESCRIBE_CLASS(GenesisDataInfo, (), (), (), (state, type))
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
        std::uint64_t     date,
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

protected:
    void calculate_hash() override;

public:
    std::string getPrevGenHash() const;
    void        setPrevGen(const BlockVariant &block);
    void        setType(BlockType value) override;
    void        setType(const std::string &value) override;

    BOOST_DESCRIBE_CLASS(GenesisBlock, (Block), (), (), (m_prevGenHash, m_dataRows))
};

inline bool operator==(const GenesisBlock &l, const GenesisBlock &r) {
    return l.getIndex() == r.getIndex() && l.getPrevHash() == r.getPrevHash()
           && l.dataService() == r.dataService() && l.dataRows() == r.dataRows();
}
