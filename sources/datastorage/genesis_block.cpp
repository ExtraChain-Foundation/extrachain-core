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
    std::string              &&type,
    std::string              &&data,
    BigNumber                  idx,
    long long                  date,
    std::string              &&prevHash,
    std::string              &&hash,
    std::string              &&prevGenHash,
    std::set<Approver>       &&signatures,
    std::set<GenesisDataRow> &&dataRows)
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

bool GenesisBlock::addRow(const GenesisDataRow &row) {
    auto res = m_dataRows.insert(row);
    return res.second;
}

int GenesisBlock::addRows(const std::set<GenesisDataRow> &rows) {
    int count = 0;
    for (const auto &row : std::as_const(rows)) {
        auto res = addRow(row);
        if (res)
            count++;
    }
    return count;
}

int GenesisBlock::addRows(const std::vector<GenesisDataRow> &rows) {
    int count = 0;
    for (const auto &row : std::as_const(rows)) {
        auto res = addRow(row);
        if (res)
            count++;
    }
    return count;
}

const std::string &GenesisBlock::getDataForSignature() const {
    return Block::getDataForSignature();
}

void GenesisBlock::calcHash() {
    SHA3 sha3(SHA3::Bits::Bits512);
    sha3.add(m_index.toStdString(16).c_str(), m_index.toStdString(16).size());

    for (const auto &data : m_dataService) {
        sha3.add(data.c_str(), data.size());
    }

    for (const GenesisDataRow &row : std::as_const(m_dataRows)) {
        sha3.add(row.actorId.toStdString().c_str(), row.actorId.toStdString().size());
        sha3.add(row.state.toStdString().c_str(), row.state.toStdString().size());
        sha3.add(row.token.toStdString().c_str(), row.token.toStdString().size());
        sha3.add(reinterpret_cast<const char *>(&row.type), sizeof(row.type));
    }

    this->m_hash = sha3.getHash();
}

const std::set<GenesisDataRow> &GenesisBlock::dataRows() const {
    return m_dataRows;
}

void GenesisBlock::setPrevGenHash(const std::string &value) {
    m_prevGenHash = value;
}

void GenesisBlock::setType(BlockType value) {
    if (value != BlockType::Genesis || value != BlockType::GenesisMerge) {
        qFatal("GenesisBlock: try to set not genesis type");
    }

    m_type = value;
}

void GenesisBlock::setType(const std::string &value) {
    if (value == "genesis") {
        m_type = BlockType::Genesis;
    } else if (value == "genesismerge") {
        m_type = BlockType::GenesisMerge;
    } else {
        qFatal("GenesisBlock: try to set not genesis type");
    }
}

std::string GenesisBlock::getPrevGenHash() const {
    return m_prevGenHash;
}

std::string GenesisBlock::toStdString() const {
    std::ostringstream oss;

    oss << "GenesisBlock { "
        << "type: " << magic_enum::enum_name(m_type) << ", "
        << "data service: [" << m_dataService.size() << "], "
        << "index: " << m_index.toStdString() << ", "
        << "date: " << QDateTime::fromMSecsSinceEpoch(m_date).toString().toStdString() << ", "
        << "prev_hash: '"
        << (m_prevHash.length() > 10 ? m_prevHash.substr(0, 5) + "..."
                                           + m_prevHash.substr(m_prevHash.size() - 5, m_prevHash.size() - 1)
                                     : m_prevHash)
               + "', "
        << "hash: '"
        << (m_hash.length() > 10
                ? m_hash.substr(0, 5) + "..." + m_hash.substr(m_hash.size() - 5, m_hash.size() - 1)
                : m_hash)
        << "', "
        << "signatures: [" << m_signatures.size() << "], "
        << "data rows: [" << m_dataRows.size() << "]"
        << " }";

    return oss.str();
}
