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

#include <boost/json.hpp>
#include <msgpack.hpp>

#include <QByteArray>
#include "contracts/contract_hash.h"
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

        void pack_json(msgpack::packer<msgpack::sbuffer> &packer, const boost::json::value &value) {
            if (value.is_null()) {
                packer.pack_nil();
            } else if (value.is_bool()) {
                packer.pack(value.as_bool());
            } else if (value.is_int64()) {
                packer.pack(value.as_int64());
            } else if (value.is_uint64()) {
                packer.pack(value.as_uint64());
            } else if (value.is_double()) {
                packer.pack(value.as_double());
            } else if (value.is_string()) {
                packer.pack(std::string_view(value.as_string().data(), value.as_string().size()));
            } else if (value.is_array()) {
                const auto &array = value.as_array();
                packer.pack_array(static_cast<std::uint32_t>(array.size()));
                for (const auto &item : array) {
                    pack_json(packer, item);
                }
            } else {
                const auto &object       = value.as_object();
                const auto  binary_value = object.if_contains("$binary");
                if (object.size() == 1 && binary_value != nullptr && binary_value->is_string()) {
                    const auto decoded =
                        QByteArray::fromBase64Encoding(QByteArray(binary_value->as_string().data(),
                                                                  static_cast<qsizetype>(
                                                                      binary_value->as_string().size())),
                                                       QByteArray::Base64UrlEncoding
                                                           | QByteArray::AbortOnBase64DecodingErrors);
                    if (!decoded) {
                        throw std::invalid_argument("Invalid $binary value");
                    }
                    const auto &bytes = decoded.decoded;
                    pack_binary(packer,
                                std::span(reinterpret_cast<const std::uint8_t *>(bytes.constData()),
                                          static_cast<std::size_t>(bytes.size())));
                    return;
                }
                packer.pack_map(static_cast<std::uint32_t>(object.size()));
                for (const auto &item : object) {
                    packer.pack(std::string_view(item.key().data(), item.key().size()));
                    pack_json(packer, item.value());
                }
            }
        }

        boost::json::value unpack_json(const msgpack::object &value) {
            switch (value.type) {
            case msgpack::type::NIL:
                return nullptr;
            case msgpack::type::BOOLEAN:
                return value.via.boolean;
            case msgpack::type::POSITIVE_INTEGER:
                return value.via.u64;
            case msgpack::type::NEGATIVE_INTEGER:
                return value.via.i64;
            case msgpack::type::FLOAT32:
            case msgpack::type::FLOAT64:
                return value.via.f64;
            case msgpack::type::STR:
                return boost::json::string(value.via.str.ptr, value.via.str.size);
            case msgpack::type::BIN: {
                const auto          bytes = binary(value);
                boost::json::object object;
                object["$binary"] =
                    QByteArray(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()))
                        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)
                        .toStdString();
                return object;
            }
            case msgpack::type::ARRAY: {
                boost::json::array array;
                array.reserve(value.via.array.size);
                for (std::uint32_t index = 0; index < value.via.array.size; ++index) {
                    array.push_back(unpack_json(value.via.array.ptr[index]));
                }
                return array;
            }
            case msgpack::type::MAP: {
                boost::json::object object;
                for (std::uint32_t index = 0; index < value.via.map.size; ++index) {
                    const auto &item = value.via.map.ptr[index];
                    if (item.key.type != msgpack::type::STR) {
                        throw msgpack::type_error();
                    }
                    object[std::string(item.key.via.str.ptr, item.key.via.str.size)] = unpack_json(item.val);
                }
                return object;
            }
            default:
                throw msgpack::type_error();
            }
        }

    } // namespace

    std::vector<std::uint8_t> encode_request(const ExecutionContext       &context,
                                             std::string_view              method,
                                             std::span<const std::uint8_t> arguments,
                                             std::span<const std::uint8_t> state,
                                             const VerifiedInputs         &verified) {
        msgpack::sbuffer buffer;
        msgpack::packer  packer(buffer);
        packer.pack_array(6);
        packer.pack_array(5);
        packer.pack(context.sender);
        packer.pack(context.caller);
        packer.pack(context.contract_id);
        packer.pack(context.block);
        packer.pack(context.depth);
        packer.pack(method);
        pack_binary(packer, arguments);
        pack_binary(packer, state);
        packer.pack_array(2);
        packer.pack_array(static_cast<std::uint32_t>(verified.dag.size()));
        for (const auto &proof : verified.dag) {
            packer.pack_array(3);
            packer.pack(proof.transaction_hash);
            packer.pack(proof.section);
            packer.pack(proof.confirmations);
        }
        packer.pack_array(static_cast<std::uint32_t>(verified.dfs.size()));
        for (const auto &proof : verified.dfs) {
            packer.pack_array(3);
            packer.pack(proof.file_id);
            packer.pack(proof.owner_id);
            packer.pack(proof.content_hash);
        }
        packer.pack(ContractAbiVersion);
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return { begin, begin + buffer.size() };
    }

    std::vector<std::uint8_t> encode_string(std::string_view value) {
        msgpack::sbuffer buffer;
        msgpack::pack(buffer, value);
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return { begin, begin + buffer.size() };
    }

    std::expected<std::vector<std::uint8_t>, ContractFailure> encode_json(std::string_view json) {
        if (json.size() > ExecutionLimits {}.input_bytes) {
            return std::unexpected(
                ContractFailure { ContractError::InvalidArguments, "JSON arguments are too large" });
        }
        try {
            const auto       parsed = boost::json::parse(json);
            msgpack::sbuffer buffer;
            msgpack::packer  packer(buffer);
            pack_json(packer, parsed);
            const auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
            return std::vector<std::uint8_t>(begin, begin + buffer.size());
        } catch (const std::exception &error) {
            return std::unexpected(ContractFailure { ContractError::InvalidArguments, error.what() });
        }
    }

    std::expected<std::string, ContractFailure> decode_json(std::span<const std::uint8_t> value) {
        try {
            std::size_t offset = 0;
            const auto  handle =
                msgpack::unpack(reinterpret_cast<const char *>(value.data()), value.size(), offset);
            if (offset != value.size()) {
                return std::unexpected(
                    ContractFailure { ContractError::InvalidResponse, "MessagePack value has trailing data" });
            }
            return boost::json::serialize(unpack_json(handle.get()));
        } catch (const std::exception &error) {
            return std::unexpected(ContractFailure { ContractError::InvalidResponse, error.what() });
        }
    }

    std::expected<ContractOutput, ContractFailure> decode_response(std::span<const std::uint8_t> response) {
        try {
            std::size_t offset = 0;
            auto        handle =
                msgpack::unpack(reinterpret_cast<const char *>(response.data()), response.size(), offset);
            const auto &root = handle.get();
            if (offset != response.size() || root.type != msgpack::type::ARRAY || root.via.array.size != 6) {
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
            if (items[3].via.array.size > ContractMaximumEvents) {
                return std::unexpected(
                    ContractFailure { ContractError::TooManyEvents, "Contract emitted too many events" });
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
            if (items[4].type != msgpack::type::ARRAY) {
                throw msgpack::type_error();
            }
            if (items[4].via.array.size > ContractMaximumEffects) {
                return std::unexpected(
                    ContractFailure { ContractError::TooManyEffects, "Contract emitted too many effects" });
            }
            result.effects.reserve(items[4].via.array.size);
            for (std::uint32_t index = 0; index < items[4].via.array.size; ++index) {
                const auto &effect = items[4].via.array.ptr[index];
                if (effect.type != msgpack::type::ARRAY || effect.via.array.size != 4) {
                    throw msgpack::type_error();
                }
                std::string    kind;
                ContractEffect decoded;
                effect.via.array.ptr[0].convert(kind);
                if (kind == "contract_call") {
                    decoded.kind = ContractEffectKind::ContractCall;
                } else {
                    throw msgpack::type_error();
                }
                effect.via.array.ptr[1].convert(decoded.target);
                effect.via.array.ptr[2].convert(decoded.operation);
                decoded.arguments = binary(effect.via.array.ptr[3]);
                result.effects.push_back(std::move(decoded));
            }
            if (items[5].type == msgpack::type::STR) {
                std::string error;
                items[5].convert(error);
                result.error = std::move(error);
            } else if (items[5].type != msgpack::type::NIL) {
                throw msgpack::type_error();
            }
            return result;
        } catch (const std::exception &error) {
            return std::unexpected(ContractFailure { ContractError::InvalidResponse, error.what() });
        }
    }

    std::vector<std::uint8_t> encode_effects(std::span<const ContractEffect> effects) {
        msgpack::sbuffer buffer;
        msgpack::packer  packer(buffer);
        packer.pack_array(static_cast<std::uint32_t>(effects.size()));
        for (const auto &effect : effects) {
            packer.pack_array(4);
            packer.pack("contract_call");
            packer.pack(effect.target);
            packer.pack(effect.operation);
            pack_binary(packer, effect.arguments);
        }
        const auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return { begin, begin + buffer.size() };
    }

    std::string effect_hash(std::span<const ContractEffect> effects) {
        return content_hash(encode_effects(effects));
    }

} // namespace ExtraChain::Contracts::Codec
