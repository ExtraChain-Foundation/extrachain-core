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

#include <expected>
#include <string>
#include <string_view>

#include "extrachain_global.h"

namespace Compression {

enum class Error {
    CompressFailed,
    DecompressFailed,
    InvalidInput,
    BufferTooSmall,
    DictLoadFailed
};

constexpr int DEFAULT_LEVEL = 3;

/**
 * One-shot compress. Optional raw dictionary improves ratio for small messages.
 */
EXTRACHAIN_EXPORT std::expected<std::string, Error>
compress(std::string_view data, std::string_view dict = {}, int level = DEFAULT_LEVEL);

/**
 * One-shot decompress. The dict passed in must match the one used for compression.
 */
EXTRACHAIN_EXPORT std::expected<std::string, Error>
decompress(std::string_view data, std::string_view dict = {});

/**
 * Streaming compression context with a loaded raw dictionary.
 * Reuse across many compress_frame calls to amortize dict loading.
 */
class EXTRACHAIN_EXPORT Context {
public:
    Context(std::string_view dict = {}, int level = DEFAULT_LEVEL);
    ~Context();

    Context(const Context &)            = delete;
    Context &operator=(const Context &) = delete;
    Context(Context &&other) noexcept;
    Context &operator=(Context &&other) noexcept;

    std::expected<std::string, Error> compress_frame(std::string_view data) const;
    std::expected<std::string, Error> decompress_frame(std::string_view data) const;

private:
    struct Impl;
    Impl *impl_;
};

} // namespace Compression
