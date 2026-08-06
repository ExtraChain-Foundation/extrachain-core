/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "contracts/contract_codec.h"

#include <limits>

#include <msgpack.hpp>

namespace ExtraChain::Contracts::Codec {
    namespace {

        void pack_binary(msgpack::packer<msgpack::sbuffer> &packer, std::span<const std::uint8_t> value) {
            if (value.size() > std::numeric_limits<std::uint32_t>::max()) {
                throw std::length_error("MessagePack binary value is too large");
            }
            packer.pack_bin(static_cast<std::uint32_t>(value.size()));
            packer.pack_bin_body(reinterpret_cast<const char *>(value.data()),
                                 static_cast<std::uint32_t>(value.size()));
        }

        std::vector<std::uint8_t> binary(const msgpack::object &object) {
            if (object.type != msgpack::type::BIN) {
                throw msgpack::type_error();
            }
            auto *begin = reinterpret_cast<const std::uint8_t *>(object.via.bin.ptr);
            return { begin, begin + object.via.bin.size };
        }

    } // namespace

    std::vector<std::uint8_t> encode_request(std::string_view              sender,
                                             std::string_view              method,
                                             std::span<const std::uint8_t> arguments,
                                             std::span<const std::uint8_t> state,
                                             std::uint64_t                 block) {
        msgpack::sbuffer buffer;
        msgpack::packer  packer(buffer);
        packer.pack_array(6);
        packer.pack(sender);
        packer.pack(method);
        pack_binary(packer, arguments);
        pack_binary(packer, state);
        packer.pack(block);
        packer.pack(std::uint32_t { 1 });
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return { begin, begin + buffer.size() };
    }

    std::vector<std::uint8_t> encode_string(std::string_view value) {
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, value);
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return { begin, begin + buffer.size() };
    }

    std::expected<ContractOutput, ContractFailure> decode_response(std::span<const std::uint8_t> response) {
        try {
            std::size_t offset = 0;
            auto        handle =
                msgpack::unpack(reinterpret_cast<const char *>(response.data()), response.size(), offset);
            const auto &root = handle.get();
            if (offset != response.size() || root.type != msgpack::type::ARRAY || root.via.array.size != 5) {
                return std::unexpected(
                    ContractFailure { ContractError::InvalidResponse, "Contract returned an invalid response" });
            }

            const auto    *items = root.via.array.ptr;
            ContractOutput result;
            items[0].convert(result.ok);
            result.state = binary(items[1]);
            result.data  = binary(items[2]);
            if (items[3].type != msgpack::type::ARRAY) {
                throw msgpack::type_error();
            }
            result.events.reserve(items[3].via.array.size);
            for (std::uint32_t index = 0; index < items[3].via.array.size; ++index) {
                const auto &event = items[3].via.array.ptr[index];
                if (event.type != msgpack::type::ARRAY || event.via.array.size != 2) {
                    throw msgpack::type_error();
                }
                ContractEvent decoded;
                event.via.array.ptr[0].convert(decoded.topic);
                decoded.data = binary(event.via.array.ptr[1]);
                result.events.push_back(std::move(decoded));
            }
            if (items[4].type == msgpack::type::STR) {
                std::string error;
                items[4].convert(error);
                result.error = std::move(error);
            } else if (items[4].type != msgpack::type::NIL) {
                throw msgpack::type_error();
            }
            return result;
        } catch (const std::exception &error) {
            return std::unexpected(ContractFailure { ContractError::InvalidResponse, error.what() });
        }
    }

} // namespace ExtraChain::Contracts::Codec
