/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "utils/legacy_compression.h"

#include <array>
#include <limits>

#include <zlib.h>

namespace LegacyCompression {
    namespace {
        constexpr std::size_t HeaderSize = sizeof(std::uint32_t);

        void store_size(std::string& output, std::uint32_t size) {
            output[0] = static_cast<char>((size >> 24U) & 0xffU);
            output[1] = static_cast<char>((size >> 16U) & 0xffU);
            output[2] = static_cast<char>((size >> 8U) & 0xffU);
            output[3] = static_cast<char>(size & 0xffU);
        }
    } // namespace

    std::expected<std::uint32_t, Error> declared_size(std::string_view data) {
        if (data.size() < HeaderSize) {
            return std::unexpected(Error::InvalidInput);
        }
        const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
        return (static_cast<std::uint32_t>(bytes[0]) << 24U) | (static_cast<std::uint32_t>(bytes[1]) << 16U)
               | (static_cast<std::uint32_t>(bytes[2]) << 8U) | static_cast<std::uint32_t>(bytes[3]);
    }

    std::expected<std::string, Error> compress(std::string_view data) {
        if (data.size() > std::numeric_limits<std::uint32_t>::max()
            || data.size() > std::numeric_limits<uLong>::max()) {
            return std::unexpected(Error::SizeLimit);
        }

        const auto  source_size = static_cast<uLong>(data.size());
        auto        capacity    = compressBound(source_size);
        std::string output(HeaderSize + capacity, '\0');
        store_size(output, static_cast<std::uint32_t>(data.size()));
        auto       destination_size = capacity;
        const auto result           = compress2(reinterpret_cast<Bytef*>(output.data() + HeaderSize),
                                      &destination_size,
                                      reinterpret_cast<const Bytef*>(data.data()),
                                      source_size,
                                      Z_DEFAULT_COMPRESSION);
        if (result != Z_OK) {
            return std::unexpected(Error::CompressFailed);
        }
        output.resize(HeaderSize + destination_size);
        return output;
    }

    std::expected<std::string, Error> decompress(std::string_view data, std::size_t size_limit) {
        const auto size = declared_size(data);
        if (!size.has_value()) {
            return std::unexpected(size.error());
        }
        if (size.value() > size_limit) {
            return std::unexpected(Error::SizeLimit);
        }
        if (data.size() - HeaderSize > std::numeric_limits<uLong>::max()) {
            return std::unexpected(Error::SizeLimit);
        }

        std::string          output(size.value(), '\0');
        std::array<Bytef, 1> empty_output {};
        auto*      destination = output.empty() ? empty_output.data() : reinterpret_cast<Bytef*>(output.data());
        auto       destination_size = static_cast<uLongf>(output.empty() ? empty_output.size() : output.size());
        const auto result           = uncompress(destination,
                                       &destination_size,
                                       reinterpret_cast<const Bytef*>(data.data() + HeaderSize),
                                       static_cast<uLong>(data.size() - HeaderSize));
        if (result != Z_OK || destination_size != output.size()) {
            return std::unexpected(Error::DecompressFailed);
        }
        return output;
    }

} // namespace LegacyCompression
