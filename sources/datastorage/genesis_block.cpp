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

#include "datastorage/genesis_block.h"

GenesisBlock::GenesisBlock()
    : Block() {
    this->m_type = BlockType::Genesis;
}

GenesisBlock::GenesisBlock(const GenesisBlock &block)
    : Block(block) {
    this->m_prevGenHash = block.getPrevGenHash();
}

GenesisBlock::GenesisBlock(const std::string &serialized) {
    deserialize(serialized);
}

GenesisBlock::GenesisBlock(const std::string &_data, const Block &prevBlock, const std::string &prevGenHash)
    : Block(_data, prevBlock)
    , m_prevGenHash(prevGenHash) {
    this->m_type = BlockType::Genesis;
}

GenesisBlock::GenesisBlock(
    std::string &&type,
    std::string &&data,
    BigNumber idx,
    long long date,
    std::string &&prevHash,
    std::string &&hash,
    std::string &&prevGenHash,
    std::vector<Approvers> &&signatures,
    std::vector<GenesisDataRow> &&dataRows)
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
}

void GenesisBlock::addRow(const GenesisDataRow &row) {
    m_dataRows.push_back(row);
}

const std::string &GenesisBlock::getDataForSignature() const {
    return Block::getDataForSignature();
}

std::string GenesisBlock::getDataForHash() const {
    return Block::getDataForHash();
}

bool GenesisBlock::deserialize(const std::string &serialized) {
    if (serialized.empty()) {
        return false;
    } else {
        *this = MessagePack::deserialize<GenesisBlock>(Utils::bytesDecodeStdString(serialized));
        return true;
    }
}

std::string GenesisBlock::serialize() const {
    return Utils::bytesEncodeStdString(MessagePack::serialize(*this));
}

const std::vector<GenesisDataRow> &GenesisBlock::dataRows() const {
    return m_dataRows;
}

void GenesisBlock::setPrevGenHash(const std::string &value) {
    m_prevGenHash = value;
}

std::string GenesisBlock::getPrevGenHash() const {
    return m_prevGenHash;
}
