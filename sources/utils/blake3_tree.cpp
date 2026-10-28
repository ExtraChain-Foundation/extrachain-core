#include "utils/blake3_tree.h"

#include <bit>
#include <string>

namespace Utils::Blake3Tree {
    namespace {
        using Words = std::array<std::uint32_t, 8>;
        using Block = std::array<std::uint32_t, 16>;

        constexpr Words                       IV { 0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
                             0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19 };
        constexpr std::array<std::size_t, 16> Permutation { 2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8 };
        constexpr std::uint32_t               ChunkStart = 1;
        constexpr std::uint32_t               ChunkEnd   = 2;
        constexpr std::uint32_t               Parent     = 4;
        constexpr std::uint32_t               Root       = 8;
        constexpr std::size_t                 BlockBytes = 64;

        void mix(Block&        state,
                 std::size_t   a,
                 std::size_t   b,
                 std::size_t   c,
                 std::size_t   d,
                 std::uint32_t x,
                 std::uint32_t y) {
            state[a] = state[a] + state[b] + x;
            state[d] = std::rotr(state[d] ^ state[a], 16);
            state[c] = state[c] + state[d];
            state[b] = std::rotr(state[b] ^ state[c], 12);
            state[a] = state[a] + state[b] + y;
            state[d] = std::rotr(state[d] ^ state[a], 8);
            state[c] = state[c] + state[d];
            state[b] = std::rotr(state[b] ^ state[c], 7);
        }

        Words compress(const Words&  value,
                       Block         message,
                       std::uint64_t counter,
                       std::uint32_t length,
                       std::uint32_t flags) {
            Block state { value[0],
                          value[1],
                          value[2],
                          value[3],
                          value[4],
                          value[5],
                          value[6],
                          value[7],
                          IV[0],
                          IV[1],
                          IV[2],
                          IV[3],
                          static_cast<std::uint32_t>(counter),
                          static_cast<std::uint32_t>(counter >> 32),
                          length,
                          flags };
            for (int round = 0; round < 7; ++round) {
                mix(state, 0, 4, 8, 12, message[0], message[1]);
                mix(state, 1, 5, 9, 13, message[2], message[3]);
                mix(state, 2, 6, 10, 14, message[4], message[5]);
                mix(state, 3, 7, 11, 15, message[6], message[7]);
                mix(state, 0, 5, 10, 15, message[8], message[9]);
                mix(state, 1, 6, 11, 12, message[10], message[11]);
                mix(state, 2, 7, 8, 13, message[12], message[13]);
                mix(state, 3, 4, 9, 14, message[14], message[15]);
                Block permuted;
                for (std::size_t index = 0; index < permuted.size(); ++index)
                    permuted[index] = message[Permutation[index]];
                message = permuted;
            }
            Words result;
            for (std::size_t index = 0; index < result.size(); ++index)
                result[index] = state[index] ^ state[index + 8];
            return result;
        }

        Block block_words(std::string_view bytes) {
            Block result {};
            for (std::size_t index = 0; index < bytes.size(); ++index)
                result[index / 4] |= std::uint32_t(static_cast<unsigned char>(bytes[index])) << (8 * (index % 4));
            return result;
        }

        Words chunk_words(std::string_view chunk, std::uint64_t index, bool root) {
            Words      value  = IV;
            const auto blocks = chunk.empty() ? 1 : (chunk.size() + BlockBytes - 1) / BlockBytes;
            for (std::size_t block = 0; block < blocks; ++block) {
                const auto piece = chunk.substr(block * BlockBytes, BlockBytes);
                const bool last  = block + 1 == blocks;
                value =
                    compress(value,
                             block_words(piece),
                             index,
                             static_cast<std::uint32_t>(piece.size()),
                             (block == 0 ? ChunkStart : 0) | (last ? ChunkEnd : 0) | (last && root ? Root : 0));
            }
            return value;
        }

        Words parent_words(const ChainingValue& left, const ChainingValue& right, bool root) {
            Block message {};
            for (std::size_t index = 0; index < 32; ++index) {
                message[index / 4] |= std::uint32_t(left[index]) << (8 * (index % 4));
                message[8 + index / 4] |= std::uint32_t(right[index]) << (8 * (index % 4));
            }
            return compress(IV, message, 0, BlockBytes, Parent | (root ? Root : 0));
        }

        ChainingValue bytes_of(const Words& words) {
            ChainingValue result;
            for (std::size_t index = 0; index < result.size(); ++index)
                result[index] = static_cast<std::uint8_t>(words[index / 4] >> (8 * (index % 4)));
            return result;
        }

        // A subtree of more than one chunk keeps the largest power of two chunks on its left that leaves at
        // least one chunk for the right.
        std::uint64_t left_chunks(std::uint64_t count) {
            return std::bit_floor(count - 1);
        }

        std::optional<ChainingValue> subtree_value(const NodeReader& read,
                                                   std::uint64_t     start,
                                                   std::uint64_t     count) {
            if (std::has_single_bit(count) && start % count == 0)
                return read(static_cast<std::uint32_t>(std::countr_zero(count)), start / count);
            const auto left  = left_chunks(count);
            const auto first = subtree_value(read, start, left);
            const auto rest  = subtree_value(read, start + left, count - left);
            if (!first.has_value() || !rest.has_value())
                return std::nullopt;
            return parent_value(first.value(), rest.value());
        }

        std::string hex(const ChainingValue& value) {
            static constexpr char Digits[] = "0123456789abcdef";
            std::string           result;
            for (const auto byte : value) {
                result.push_back(Digits[byte >> 4]);
                result.push_back(Digits[byte & 15]);
            }
            return result;
        }
    } // namespace

    std::uint64_t chunk_count(std::uint64_t bytes) {
        return bytes == 0 ? 1 : bytes / ChunkBytes + (bytes % ChunkBytes != 0);
    }

    ChainingValue chunk_value(std::string_view chunk, std::uint64_t index) {
        return bytes_of(chunk_words(chunk, index, false));
    }

    ChainingValue parent_value(const ChainingValue& left, const ChainingValue& right) {
        return bytes_of(parent_words(left, right, false));
    }

    std::vector<std::vector<ChainingValue>> full_levels(std::span<const ChainingValue> chunks) {
        std::vector<std::vector<ChainingValue>> levels { { chunks.begin(), chunks.end() } };
        while (levels.back().size() >= 2) {
            const auto&                below = levels.back();
            std::vector<ChainingValue> level;
            level.reserve(below.size() / 2);
            for (std::size_t index = 0; index + 1 < below.size(); index += 2)
                level.push_back(parent_value(below[index], below[index + 1]));
            levels.push_back(std::move(level));
        }
        return levels;
    }

    std::optional<std::vector<ChainingValue>> chunk_path(std::uint64_t     count,
                                                         std::uint64_t     index,
                                                         const NodeReader& read) {
        if (index >= count || !read)
            return std::nullopt;
        std::vector<ChainingValue> path;
        std::uint64_t              start = 0;
        while (count > 1) {
            const auto                   left = left_chunks(count);
            std::optional<ChainingValue> sibling;
            if (index - start < left) {
                sibling = subtree_value(read, start + left, count - left);
                count   = left;
            } else {
                sibling = subtree_value(read, start, left);
                start += left;
                count -= left;
            }
            if (!sibling.has_value())
                return std::nullopt;
            path.push_back(sibling.value());
        }
        // Built from the root down; a verifier folds it from the chunk up.
        return std::vector<ChainingValue>(path.rbegin(), path.rend());
    }

    std::vector<ChainingValue> chunk_path(std::span<const ChainingValue> chunks, std::uint64_t index) {
        const auto levels = full_levels(chunks);
        const auto path   = chunk_path(chunks.size(), index, [&](std::uint32_t level, std::uint64_t block) {
            return level < levels.size() && block < levels[level].size()
                         ? std::optional<ChainingValue>(levels[level][block])
                         : std::nullopt;
        });
        return path.value_or(std::vector<ChainingValue> {});
    }

    bool verify_chunk(std::string_view               chunk,
                      std::uint64_t                  index,
                      std::uint64_t                  bytes,
                      std::span<const ChainingValue> path,
                      std::string_view               root) {
        const auto count = chunk_count(bytes);
        if (index >= count)
            return false;
        const auto expected = index + 1 < count ? ChunkBytes : bytes - (count - 1) * ChunkBytes;
        if (chunk.size() != expected)
            return false;
        if (count == 1)
            return path.empty() && hex(bytes_of(chunk_words(chunk, 0, true))) == root;
        // Which side the chunk takes at each level, from the root down.
        std::vector<bool> on_left;
        for (std::uint64_t remaining = count, offset = index; remaining > 1;) {
            const auto left = left_chunks(remaining);
            on_left.push_back(offset < left);
            if (offset < left) {
                remaining = left;
            } else {
                remaining -= left;
                offset -= left;
            }
        }
        if (path.size() != on_left.size())
            return false;
        auto value = chunk_value(chunk, index);
        for (std::size_t level = 0; level < path.size(); ++level) {
            const bool left = on_left[on_left.size() - 1 - level];
            const bool top  = level + 1 == path.size();
            const auto words =
                left ? parent_words(value, path[level], top) : parent_words(path[level], value, top);
            value = bytes_of(words);
        }
        return hex(value) == root;
    }
} // namespace Utils::Blake3Tree
