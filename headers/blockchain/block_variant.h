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
    BlockVariant() = default;
    explicit BlockVariant(std::variant<Block, GenesisBlock> block);
    explicit BlockVariant(Block block);
    explicit BlockVariant(GenesisBlock block);

    bool isEmpty() const;

    BlockType              getType() const;
    BigNumber              id() const;
    std::uint64_t          getDate() const;
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
    void sign(const Actor<KeyPrivate>& actor);
    bool verify(const Actor<KeyPublic>& actor) const;

    bool isBlock() const;
    bool is_genesis() const;

    std::optional<std::reference_wrapper<const Block>>        getBlockConst() const;
    std::optional<std::reference_wrapper<const GenesisBlock>> getGenesisBlockConst() const;
    std::optional<std::reference_wrapper<Block>>              getBlock();
    std::optional<std::reference_wrapper<GenesisBlock>>       getGenesisBlock();

    bool operator==(const BlockVariant& other) const {
        if (is_genesis()) {
            return getGenesisBlockConst() == other.getGenesisBlockConst();
        }
        return getBlockConst() == other.getBlockConst();
    }

public:
    std::variant<Block, GenesisBlock> m_block;

    // BOOST_DESCRIBE_CLASS(BlockVariant, (), (), (), (m_block))
};

namespace msgpack {
    MSGPACK_API_VERSION_NAMESPACE(MSGPACK_DEFAULT_API_NS) {
        namespace adaptor {
            template <>
            struct convert<BlockVariant> {
                msgpack::object const& operator()(msgpack::object const& o, BlockVariant& v) const {
                    try {
                        Block block;
                        o.convert(block);
                        v.m_block = std::move(block);
                    } catch (const msgpack::type_error&) {
                        try {
                            GenesisBlock genesis;
                            o.convert(genesis);
                            v.m_block = std::move(genesis);
                        } catch (const msgpack::type_error&) {
                            throw msgpack::type_error();
                        }
                    }
                    return o;
                }
            };

            template <>
            struct pack<BlockVariant> {
                template <typename Stream>
                msgpack::packer<Stream>& operator()(msgpack::packer<Stream>& o, const BlockVariant& v) const {
                    std::visit(
                        [&o](const auto& block) {
                            o.pack(block);
                        },
                        v.m_block);
                    return o;
                }
            };
        } // namespace adaptor
    }
} // namespace msgpack
