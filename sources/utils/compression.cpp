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

#include "utils/compression.h"
#include <zdict.h>

namespace Utils {

Compressor::Compressor(int level)
    : level_(level) {
    cctx_ = ZSTD_createCCtx();
    dctx_ = ZSTD_createDCtx();
}

Compressor::~Compressor() {
    if (cdict_) ZSTD_freeCDict(cdict_);
    if (ddict_) ZSTD_freeDDict(ddict_);
    if (cctx_) ZSTD_freeCCtx(cctx_);
    if (dctx_) ZSTD_freeDCtx(dctx_);
}

Compressor::Compressor(Compressor&& other) noexcept
    : level_(other.level_)
    , cctx_(other.cctx_)
    , dctx_(other.dctx_)
    , cdict_(other.cdict_)
    , ddict_(other.ddict_)
    , dict_data_(std::move(other.dict_data_)) {
    other.cctx_  = nullptr;
    other.dctx_  = nullptr;
    other.cdict_ = nullptr;
    other.ddict_ = nullptr;
}

Compressor& Compressor::operator=(Compressor&& other) noexcept {
    if (this != &other) {
        if (cdict_) ZSTD_freeCDict(cdict_);
        if (ddict_) ZSTD_freeDDict(ddict_);
        if (cctx_) ZSTD_freeCCtx(cctx_);
        if (dctx_) ZSTD_freeDCtx(dctx_);

        level_     = other.level_;
        cctx_      = other.cctx_;
        dctx_      = other.dctx_;
        cdict_     = other.cdict_;
        ddict_     = other.ddict_;
        dict_data_ = std::move(other.dict_data_);

        other.cctx_  = nullptr;
        other.dctx_  = nullptr;
        other.cdict_ = nullptr;
        other.ddict_ = nullptr;
    }
    return *this;
}

std::expected<void, CompressionError>
Compressor::train_dict(const std::vector<std::string>& samples, size_t dict_size) {
    if (samples.empty()) {
        return std::unexpected(CompressionError::DictTrainFailed);
    }

    std::vector<size_t> sizes;
    std::string         combined;
    for (const auto& s : samples) {
        sizes.push_back(s.size());
        combined += s;
    }

    std::string dict(dict_size, '\0');
    size_t result = ZDICT_trainFromBuffer(
        dict.data(), dict_size,
        combined.data(), sizes.data(), static_cast<unsigned>(sizes.size()));

    if (ZDICT_isError(result)) {
        return std::unexpected(CompressionError::DictTrainFailed);
    }

    dict.resize(result);
    return load_dict(dict);
}

std::expected<void, CompressionError> Compressor::load_dict(const std::string& dict_data) {
    if (cdict_) ZSTD_freeCDict(cdict_);
    if (ddict_) ZSTD_freeDDict(ddict_);

    cdict_ = ZSTD_createCDict(dict_data.data(), dict_data.size(), level_);
    ddict_ = ZSTD_createDDict(dict_data.data(), dict_data.size());

    if (!cdict_ || !ddict_) {
        if (cdict_) { ZSTD_freeCDict(cdict_); cdict_ = nullptr; }
        if (ddict_) { ZSTD_freeDDict(ddict_); ddict_ = nullptr; }
        return std::unexpected(CompressionError::DictLoadFailed);
    }

    dict_data_ = dict_data;
    return {};
}

std::optional<std::string> Compressor::save_dict() const {
    if (dict_data_.empty()) {
        return std::nullopt;
    }
    return dict_data_;
}

bool Compressor::has_dict() const {
    return cdict_ != nullptr && ddict_ != nullptr;
}

std::expected<std::string, CompressionError> Compressor::compress(const std::string& data) {
    size_t bound = ZSTD_compressBound(data.size());
    std::string result(bound, '\0');

    size_t compressed_size;
    if (has_dict()) {
        compressed_size = ZSTD_compress_usingCDict(
            cctx_, result.data(), result.size(),
            data.data(), data.size(), cdict_);
    } else {
        compressed_size = ZSTD_compressCCtx(
            cctx_, result.data(), result.size(),
            data.data(), data.size(), level_);
    }

    if (ZSTD_isError(compressed_size)) {
        return std::unexpected(CompressionError::CompressFailed);
    }

    result.resize(compressed_size);
    return result;
}

std::expected<std::string, CompressionError> Compressor::decompress(const std::string& data) {
    size_t decompressed_size = ZSTD_getFrameContentSize(data.data(), data.size());
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR ||
        decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        return std::unexpected(CompressionError::InvalidData);
    }

    std::string result(decompressed_size, '\0');

    size_t actual_size;
    if (has_dict()) {
        actual_size = ZSTD_decompress_usingDDict(
            dctx_, result.data(), result.size(),
            data.data(), data.size(), ddict_);
    } else {
        actual_size = ZSTD_decompressDCtx(
            dctx_, result.data(), result.size(),
            data.data(), data.size());
    }

    if (ZSTD_isError(actual_size)) {
        return std::unexpected(CompressionError::DecompressFailed);
    }

    result.resize(actual_size);
    return result;
}

std::expected<std::string, CompressionError>
Compressor::compress_simple(const std::string& data, int level) {
    size_t bound = ZSTD_compressBound(data.size());
    std::string result(bound, '\0');

    size_t compressed_size = ZSTD_compress(
        result.data(), result.size(),
        data.data(), data.size(), level);

    if (ZSTD_isError(compressed_size)) {
        return std::unexpected(CompressionError::CompressFailed);
    }

    result.resize(compressed_size);
    return result;
}

std::expected<std::string, CompressionError>
Compressor::decompress_simple(const std::string& data) {
    size_t decompressed_size = ZSTD_getFrameContentSize(data.data(), data.size());
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR ||
        decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        return std::unexpected(CompressionError::InvalidData);
    }

    std::string result(decompressed_size, '\0');
    size_t actual_size = ZSTD_decompress(
        result.data(), result.size(),
        data.data(), data.size());

    if (ZSTD_isError(actual_size)) {
        return std::unexpected(CompressionError::DecompressFailed);
    }

    result.resize(actual_size);
    return result;
}

} // namespace Utils
