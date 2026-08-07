/*
 * ExtraChain Core
 * Copyright (C) 2026 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

#include <blake3.h>

namespace ExtraChain::Contracts {

    [[nodiscard]] inline std::string content_hash(std::span<const std::uint8_t> value) {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);
        blake3_hasher_update(&hasher, value.data(), value.size());

        std::array<std::uint8_t, BLAKE3_OUT_LEN> digest {};
        blake3_hasher_finalize(&hasher, digest.data(), digest.size());

        static constexpr char Hex[] = "0123456789abcdef";
        std::string           result(digest.size() * 2, '0');
        for (std::size_t index = 0; index < digest.size(); ++index) {
            result[index * 2]     = Hex[digest[index] >> 4];
            result[index * 2 + 1] = Hex[digest[index] & 0x0f];
        }
        return result;
    }

} // namespace ExtraChain::Contracts
