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
#include <optional>
#include <string>
#include <vector>

#include <zstd.h>

namespace Utils {

enum class CompressionError {
    Ok,
    CompressFailed,
    DecompressFailed,
    DictTrainFailed,
    DictLoadFailed,
    InvalidData,
    BufferTooSmall
};

class Compressor {
public:
    Compressor(int level = 3);
    ~Compressor();

    Compressor(const Compressor&)            = delete;
    Compressor& operator=(const Compressor&) = delete;
    Compressor(Compressor&&) noexcept;
    Compressor& operator=(Compressor&&) noexcept;

    // Train dictionary from samples
    std::expected<void, CompressionError>
    train_dict(const std::vector<std::string>& samples, size_t dict_size = 100 * 1024);

    // Load/save dictionary
    std::expected<void, CompressionError> load_dict(const std::string& dict_data);
    std::optional<std::string>            save_dict() const;
    bool                                  has_dict() const;

    // Compress/decompress (uses dict if available)
    std::expected<std::string, CompressionError> compress(const std::string& data);
    std::expected<std::string, CompressionError> decompress(const std::string& data);

    // Static helpers without dictionary
    static std::expected<std::string, CompressionError> compress_simple(const std::string& data, int level = 3);
    static std::expected<std::string, CompressionError> decompress_simple(const std::string& data);

private:
    int                level_;
    ZSTD_CCtx*         cctx_ = nullptr;
    ZSTD_DCtx*         dctx_ = nullptr;
    ZSTD_CDict*        cdict_ = nullptr;
    ZSTD_DDict*        ddict_ = nullptr;
    std::string        dict_data_;
};

} // namespace Utils
