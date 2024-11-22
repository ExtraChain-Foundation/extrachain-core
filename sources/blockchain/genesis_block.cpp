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

#include "blockchain/genesis_block.h"

#include "blockchain/block_variant.h"

#include "sha3.h"

GenesisBlock::GenesisBlock()
    : Block() {
    this->m_type = BlockType::Genesis;
}

GenesisBlock::GenesisBlock(const GenesisBlock &block)
    : Block(block) {
    this->m_prevGenHash = block.getPrevGenHash();
    this->m_dataRows    = block.dataRows();
}

GenesisBlock::GenesisBlock(
    std::string                                 &&type,
    std::string                                 &&data,
    BigNumber                                     idx,
    std::uint64_t                                 date,
    std::string                                 &&prevHash,
    std::string                                 &&hash,
    std::string                                 &&prevGenHash,
    Signatures                                  &&signatures,
    std::map<GenesisDataActor, GenesisDataInfo> &&dataRows)
    : Block(
          std::move(type),
          std::move(data),
          std::move(idx),
          date,
          std::move(prevHash),
          std::move(hash),
          std::move(signatures),
          {})
    , m_prevGenHash(std::move(prevGenHash))
    , m_dataRows(std::move(dataRows)) {
    setType(type);
}

void GenesisBlock::addRow(const ActorId &actorId, const TokenId &tokenId, const GenesisDataInfo &row) {
    m_dataRows[{ actorId, tokenId }] = row;
}

void GenesisBlock::addRows(const GenesisDataRows &dataRows) {
    for (const auto &[key, row] : dataRows) {
        m_dataRows[key] = row;
    }
}

const std::string &GenesisBlock::getDataForSignature() const {
    return Block::getDataForSignature();
}

void GenesisBlock::calculate_hash() {
    SHA3 sha3(SHA3::Bits::Bits512);
    auto index = m_index.to_string(NumeralBase::Hex);
    sha3.add(index.c_str(), index.size());

    for (const auto &data : m_dataService) {
        sha3.add(data.c_str(), data.size());
    }

    for (const auto &[key, row] : std::as_const(m_dataRows)) {
        auto &[actorId, tokenId] = key;
        sha3.add(actorId.to_string().c_str(), actorId.to_string().size());
        sha3.add(row.state.to_string().c_str(), row.state.to_string().size());
        sha3.add(tokenId.to_string().c_str(), tokenId.to_string().size());
        sha3.add(reinterpret_cast<const char *>(&row.type), sizeof(row.type));
    }

    this->m_hash = sha3.getHash();
}

const GenesisDataRows &GenesisBlock::dataRows() const {
    return m_dataRows;
}

void GenesisBlock::setPrevGen(const BlockVariant &block) {
    m_prevGenHash = block.getHash();
    m_index       = block.getIndex() + Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
}

void GenesisBlock::setType(BlockType value) {
    if (value != BlockType::Genesis) {
        eFatal("GenesisBlock: try to set not genesis type");
    }

    m_type = value;
}

void GenesisBlock::setType(const std::string &value) {
    if (value == "genesis") {
        m_type = BlockType::Genesis;
    } else {
        eFatal("GenesisBlock: try to set not genesis type");
    }
}

std::string GenesisBlock::getPrevGenHash() const {
    return m_prevGenHash;
}
