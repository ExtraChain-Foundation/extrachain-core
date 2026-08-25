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

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "extrachain_global.h"
#include "utils/bignumber.h"

namespace Pack {

using PackId = std::uint64_t;

// Fixed pack layout parameters
constexpr std::uint32_t FORMAT_VERSION    = 1;
constexpr std::uint32_t MAGIC             = 0x4B50'5845; // "EXPK" little-endian
constexpr std::size_t   SECTIONS_PER_PACK = 10000;
constexpr std::size_t   SECTIONS_PER_FRAME = 32;
constexpr int           COMPRESSION_LEVEL = 3;

enum class Error {
    OpenFailed,
    WriteFailed,
    ReadFailed,
    InvalidMagic,
    InvalidVersion,
    InvalidFormat,
    ChecksumMismatch,
    CompressionFailed,
    DecompressionFailed,
    SectionNotFound,
    NonConsecutiveSections,
    EmptyInput
};

/**
 * Immutable, memory-mapped read-only pack file with zstd-compressed section frames.
 * Sections inside one pack are consecutive integers [first, last].
 */
class EXTRACHAIN_EXPORT Reader {
public:
    ~Reader();

    Reader(const Reader &)            = delete;
    Reader &operator=(const Reader &) = delete;
    Reader(Reader &&other) noexcept;
    Reader &operator=(Reader &&other) noexcept;

    static std::expected<Reader, Error> open(const std::filesystem::path &path);

    std::optional<std::string> read(const SectionId &id) const;
    std::vector<std::pair<SectionId, std::string>>
    read_range(const SectionId &from, const SectionId &to) const;

    SectionId first_section() const;
    SectionId last_section() const;
    std::size_t count() const;
    PackId id() const;

private:
    Reader();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * One-shot writer. Takes all sections for the pack, builds dict from their content,
 * compresses in mini-frames, writes an immutable .pack file.
 *
 * sections must contain at least one entry and keys must be consecutive integers.
 */
EXTRACHAIN_EXPORT std::expected<void, Error>
write(const std::filesystem::path                      &path,
      PackId                                            pack_id,
      const std::map<SectionId, std::string>           &sections);

} // namespace Pack
