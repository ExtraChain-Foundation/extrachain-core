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

#include "blockchain/block_variant.h"

BlockVariant::BlockVariant(std::variant<Block, GenesisBlock> block)
    : m_block(std::move(block)) {
}

BlockVariant::BlockVariant(Block block)
    : m_block(std::move(block)) {
}

BlockVariant::BlockVariant(GenesisBlock block)
    : m_block(std::move(block)) {
}

bool BlockVariant::isEmpty() const {
    return std::visit(
        [](const auto& b) {
            return b.isEmpty();
        },
        m_block);
}

BlockType BlockVariant::getType() const {
    return std::visit(
        [](const auto& b) {
            return b.getType();
        },
        m_block);
}

BigNumber BlockVariant::getIndex() const {
    return std::visit(
        [](const auto& b) {
            return b.getIndex();
        },
        m_block);
}

std::set<std::string> BlockVariant::dataService() const {
    return std::visit(
        [](const auto& b) {
            return b.dataService();
        },
        m_block);
}

std::string BlockVariant::getPrevHash() const {
    return std::visit(
        [](const auto& b) {
            return b.getPrevHash();
        },
        m_block);
}

std::string BlockVariant::getPrevGenHash() const {
    if (const auto* genesisBlock = std::get_if<GenesisBlock>(&m_block)) {
        return genesisBlock->getPrevGenHash();
    }

    return "";
}

std::string BlockVariant::getHash() const {
    return std::visit(
        [](const auto& b) {
            return b.getHash();
        },
        m_block);
}

Signatures BlockVariant::signatures() const {
    return std::visit(
        [](const auto& b) {
            return b.signatures();
        },
        m_block);
}

std::set<Transaction> BlockVariant::transactions() const {
    if (isGenesisBlock()) {
        return {};
    }

    return std::visit(
        [](const auto& b) {
            return b.transactions();
        },
        m_block);
}

const GenesisDataRows& BlockVariant::dataRows() const {
    if (const auto* genesisBlock = std::get_if<GenesisBlock>(&m_block)) {
        return genesisBlock->dataRows();
    }

    static GenesisDataRows rows;
    return rows;
}

void BlockVariant::setType(BlockType type) {
    std::visit(
        [&type](auto& b) {
            b.setType(type);
        },
        m_block);
}

void BlockVariant::setPrevHash(const std::string& prevHash) {
    std::visit(
        [&prevHash](auto& b) {
            b.setPrevHash(prevHash);
        },
        m_block);
}

void BlockVariant::addSignature(const ActorId& id, const Signature& sign, bool isApprove) {
    std::visit(
        [&id, &sign, &isApprove](auto& b) {
            b.addSignature(id, sign, isApprove);
        },
        m_block);
}

void BlockVariant::sign(const Actor<KeyPrivate>& actor) {
    std::visit(
        [&actor](auto& b) {
            b.sign(actor);
        },
        m_block);
}

bool BlockVariant::verify(const Actor<KeyPublic>& actor) const {
    return std::visit(
        [&actor](auto& b) {
            return b.verify(actor) == BlockSignError::NoError;
        },
        m_block);
}

bool BlockVariant::isBlock() const {
    return std::holds_alternative<Block>(m_block);
}

bool BlockVariant::isGenesisBlock() const {
    return std::holds_alternative<GenesisBlock>(m_block);
}

std::optional<std::reference_wrapper<const Block>> BlockVariant::getBlockConst() const {
    if (auto block = std::get_if<Block>(&m_block)) {
        return std::cref(*block);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const GenesisBlock>> BlockVariant::getGenesisBlockConst() const {
    if (auto block = std::get_if<GenesisBlock>(&m_block)) {
        return std::cref(*block);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<Block>> BlockVariant::getBlock() {
    if (auto block = std::get_if<Block>(&m_block)) {
        return std::ref(*block);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<GenesisBlock>> BlockVariant::getGenesisBlock() {
    if (auto block = std::get_if<GenesisBlock>(&m_block)) {
        return std::ref(*block);
    }
    return std::nullopt;
}
