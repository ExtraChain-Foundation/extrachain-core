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

#include <zstd.h>

namespace Compression {

// ZSTD_getFrameContentSize reads the advertised size straight from the frame
// header, which is attacker-controlled for packs received over the network. Cap
// it so a few-byte frame claiming a huge size can't trigger a giant allocation
// (OOM DoS). No legitimate frame (<= a handful of sections) approaches this.
constexpr unsigned long long MAX_DECOMPRESSED_SIZE = 256ull * 1024 * 1024;

std::expected<std::string, Error>
compress(std::string_view data, std::string_view dict, int level) {
    size_t bound = ZSTD_compressBound(data.size());
    std::string out;
    out.resize(bound);

    size_t written;
    if (dict.empty()) {
        written = ZSTD_compress(out.data(), bound, data.data(), data.size(), level);
    } else {
        ZSTD_CCtx *ctx = ZSTD_createCCtx();
        if (!ctx) return std::unexpected(Error::CompressFailed);

        written = ZSTD_compress_usingDict(
            ctx, out.data(), bound, data.data(), data.size(),
            dict.data(), dict.size(), level);

        ZSTD_freeCCtx(ctx);
    }

    if (ZSTD_isError(written)) {
        return std::unexpected(Error::CompressFailed);
    }

    out.resize(written);
    return out;
}

std::expected<std::string, Error>
decompress(std::string_view data, std::string_view dict) {
    // Query the decompressed size from the frame header
    unsigned long long decompressed_size = ZSTD_getFrameContentSize(data.data(), data.size());
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR
        || decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN
        || decompressed_size > MAX_DECOMPRESSED_SIZE) {
        return std::unexpected(Error::InvalidInput);
    }

    std::string out;
    out.resize(decompressed_size);

    size_t written;
    if (dict.empty()) {
        written = ZSTD_decompress(out.data(), decompressed_size, data.data(), data.size());
    } else {
        ZSTD_DCtx *ctx = ZSTD_createDCtx();
        if (!ctx) return std::unexpected(Error::DecompressFailed);

        written = ZSTD_decompress_usingDict(
            ctx, out.data(), decompressed_size, data.data(), data.size(),
            dict.data(), dict.size());

        ZSTD_freeDCtx(ctx);
    }

    if (ZSTD_isError(written)) {
        return std::unexpected(Error::DecompressFailed);
    }

    out.resize(written);
    return out;
}

struct Context::Impl {
    ZSTD_CCtx *cctx = nullptr;
    ZSTD_DCtx *dctx = nullptr;
    ZSTD_CDict *cdict = nullptr;
    ZSTD_DDict *ddict = nullptr;
    int        level = DEFAULT_LEVEL;

    ~Impl() {
        if (cdict) ZSTD_freeCDict(cdict);
        if (ddict) ZSTD_freeDDict(ddict);
        if (cctx) ZSTD_freeCCtx(cctx);
        if (dctx) ZSTD_freeDCtx(dctx);
    }
};

Context::Context(std::string_view dict, int level)
    : impl_(new Impl()) {
    impl_->level = level;
    impl_->cctx  = ZSTD_createCCtx();
    impl_->dctx  = ZSTD_createDCtx();

    if (!dict.empty()) {
        impl_->cdict = ZSTD_createCDict(dict.data(), dict.size(), level);
        impl_->ddict = ZSTD_createDDict(dict.data(), dict.size());
    }
}

Context::~Context() {
    delete impl_;
}

Context::Context(Context &&other) noexcept
    : impl_(other.impl_) {
    other.impl_ = nullptr;
}

Context &Context::operator=(Context &&other) noexcept {
    if (this != &other) {
        delete impl_;
        impl_       = other.impl_;
        other.impl_ = nullptr;
    }
    return *this;
}

std::expected<std::string, Error> Context::compress_frame(std::string_view data) const {
    if (!impl_ || !impl_->cctx) return std::unexpected(Error::CompressFailed);

    size_t bound = ZSTD_compressBound(data.size());
    std::string out;
    out.resize(bound);

    size_t written;
    if (impl_->cdict) {
        written = ZSTD_compress_usingCDict(
            impl_->cctx, out.data(), bound, data.data(), data.size(), impl_->cdict);
    } else {
        written = ZSTD_compressCCtx(
            impl_->cctx, out.data(), bound, data.data(), data.size(), impl_->level);
    }

    if (ZSTD_isError(written)) {
        return std::unexpected(Error::CompressFailed);
    }

    out.resize(written);
    return out;
}

std::expected<std::string, Error> Context::decompress_frame(std::string_view data) const {
    if (!impl_ || !impl_->dctx) return std::unexpected(Error::DecompressFailed);

    unsigned long long decompressed_size = ZSTD_getFrameContentSize(data.data(), data.size());
    if (decompressed_size == ZSTD_CONTENTSIZE_ERROR
        || decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN
        || decompressed_size > MAX_DECOMPRESSED_SIZE) {
        return std::unexpected(Error::InvalidInput);
    }

    std::string out;
    out.resize(decompressed_size);

    size_t written;
    if (impl_->ddict) {
        written = ZSTD_decompress_usingDDict(
            impl_->dctx, out.data(), decompressed_size,
            data.data(), data.size(), impl_->ddict);
    } else {
        written = ZSTD_decompressDCtx(
            impl_->dctx, out.data(), decompressed_size, data.data(), data.size());
    }

    if (ZSTD_isError(written)) {
        return std::unexpected(Error::DecompressFailed);
    }

    out.resize(written);
    return out;
}

} // namespace Compression
