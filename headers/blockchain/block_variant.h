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

#include <variant>

#include "blockchain/block.h"
#include "blockchain/genesis_block.h"

class BlockVariant {
public:
    explicit BlockVariant(std::variant<Block, GenesisBlock> block);
    explicit BlockVariant(Block block);
    explicit BlockVariant(GenesisBlock block);

    bool isEmpty() const;

    BlockType              getType() const;
    BigNumber              getIndex() const;
    std::set<std::string>  dataService() const;
    std::string            getPrevHash() const;
    std::string            getPrevGenHash() const;
    std::string            getHash() const;
    Signatures             signatures() const;
    std::set<Transaction>  transactions() const;
    const GenesisDataRows& dataRows() const;

    void setType(BlockType type);
    void setPrevHash(const std::string& prevHash);

    void addSignature(const ActorId& id, const Signature& sign, bool isApprove);
    void sign(const std::shared_ptr<Actor<KeyPrivate>> actor);
    bool verify(const Actor<KeyPublic>& actor) const;

    bool isBlock() const;
    bool isGenesisBlock() const;

    std::optional<std::reference_wrapper<const Block>>        getBlockConst() const;
    std::optional<std::reference_wrapper<const GenesisBlock>> getGenesisBlockConst() const;
    std::optional<std::reference_wrapper<Block>>              getBlock();
    std::optional<std::reference_wrapper<GenesisBlock>>       getGenesisBlock();

    bool operator==(const BlockVariant& other) const {
        if (isGenesisBlock()) {
            return getGenesisBlockConst() == other.getGenesisBlockConst();
        }
        return getBlockConst() == other.getBlockConst();
    }

private:
    std::variant<Block, GenesisBlock> m_block;

    BOOST_DESCRIBE_CLASS(BlockVariant, (), (), (), (m_block))
};
