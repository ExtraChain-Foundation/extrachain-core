#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "extrachain_global.h"

// BLAKE3 hashes an input as a Merkle tree of 1 KiB chunks. A DFS row signs that root, so one chunk with
// the chaining values of its siblings proves those bytes belong to the signed file without a second
// commitment. The library API hides the tree; this follows the BLAKE3 specification directly.
namespace Utils::Blake3Tree {
    inline constexpr std::size_t ChunkBytes = 1024;
    using ChainingValue                     = std::array<std::uint8_t, 32>;

    // How many chunks BLAKE3 splits an input of this many bytes into: an empty input is one empty chunk.
    EXTRACHAIN_EXPORT std::uint64_t chunk_count(std::uint64_t bytes);
    // The chaining value of chunk `index` in an input of more than one chunk.
    EXTRACHAIN_EXPORT ChainingValue chunk_value(std::string_view chunk, std::uint64_t index);
    EXTRACHAIN_EXPORT ChainingValue parent_value(const ChainingValue& left, const ChainingValue& right);
    // Sibling chaining values from the chunk up to the root, from the values of every chunk.
    EXTRACHAIN_EXPORT std::vector<ChainingValue> chunk_path(std::span<const ChainingValue> chunks,
                                                            std::uint64_t                  index);

    // The chaining value of the full subtree of 2^level chunks that starts at chunk block << level; level 0
    // is a chunk. Every subtree on the left of a BLAKE3 node is such a block, so an index that keeps all of
    // them answers a path with O(log^2 n) reads instead of rehashing the file.
    using NodeReader = std::function<std::optional<ChainingValue>(std::uint32_t level, std::uint64_t block)>;
    // Every full block, level by level: level l has one value per 2^l chunks that fit whole.
    EXTRACHAIN_EXPORT std::vector<std::vector<ChainingValue>> full_levels(std::span<const ChainingValue> chunks);
    EXTRACHAIN_EXPORT std::optional<std::vector<ChainingValue>> chunk_path(std::uint64_t     count,
                                                                           std::uint64_t     index,
                                                                           const NodeReader& read);
    // Whether chunk `index` of an input of `bytes` bytes reaches `root`, a lowercase hex BLAKE3 hash.
    EXTRACHAIN_EXPORT bool verify_chunk(std::string_view               chunk,
                                        std::uint64_t                  index,
                                        std::uint64_t                  bytes,
                                        std::span<const ChainingValue> path,
                                        std::string_view               root);
} // namespace Utils::Blake3Tree
