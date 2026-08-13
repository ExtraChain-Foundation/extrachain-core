/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

#include "extrachain_global.h"

namespace LegacyCompression {

    enum class Error {
        InvalidInput,
        SizeLimit,
        CompressFailed,
        DecompressFailed
    };

    EXTRACHAIN_EXPORT std::expected<std::string, Error> compress(std::string_view data);
    EXTRACHAIN_EXPORT std::expected<std::string, Error> decompress(std::string_view data, std::size_t size_limit);
    EXTRACHAIN_EXPORT std::expected<std::uint32_t, Error> declared_size(std::string_view data);

} // namespace LegacyCompression
