#include "test_support.h"
#include "utils/blake3_tree.h"
#include "utils/hash.h"

#include <string>
#include <vector>

using namespace Utils::Blake3Tree;

// Every chunk of a file must reach the root the library computes for the whole file, and nothing else may.
int main() {
    for (const std::uint64_t bytes :
         { 0ULL, 1ULL, 1023ULL, 1024ULL, 1025ULL, 2048ULL, 3073ULL, 4096ULL, 5120ULL, 8193ULL, 17509ULL }) {
        std::string data(bytes, '\0');
        for (std::uint64_t index = 0; index < bytes; ++index)
            data[index] = static_cast<char>((index * 131 + bytes) % 251);
        const auto root  = Utils::calculate_hash(data);
        const auto count = chunk_count(bytes);
        TEST_REQUIRE_EQ(count, bytes == 0 ? 1 : (bytes + ChunkBytes - 1) / ChunkBytes);
        const auto piece = [&](std::uint64_t index) {
            return std::string_view(data).substr(index * ChunkBytes, ChunkBytes);
        };
        std::vector<ChainingValue> values;
        for (std::uint64_t index = 0; index < count; ++index)
            values.push_back(chunk_value(piece(index), index));
        for (std::uint64_t index = 0; index < count; ++index) {
            const auto path = count == 1 ? std::vector<ChainingValue> {} : chunk_path(values, index);
            TEST_REQUIRE(verify_chunk(piece(index), index, bytes, path, root));
            // The tree commits to bytes, not to the length: an inner chunk cannot tell the size, which a
            // verifier takes from the signed row. The last chunk's length must match it.
            if (index + 1 == count && bytes % ChunkBytes != 0)
                TEST_REQUIRE(!verify_chunk(piece(index), index, bytes + 1, path, root));
            if (count > 1) {
                TEST_REQUIRE(!verify_chunk(piece(index), (index + 1) % count, bytes, path, root));
                auto shortened = path;
                shortened.pop_back();
                TEST_REQUIRE(!verify_chunk(piece(index), index, bytes, shortened, root));
                auto changed_path = path;
                changed_path.front()[0] ^= 1;
                TEST_REQUIRE(!verify_chunk(piece(index), index, bytes, changed_path, root));
            }
            if (!piece(index).empty()) {
                std::string changed(piece(index));
                changed.back() ^= 1;
                TEST_REQUIRE(!verify_chunk(changed, index, bytes, path, root));
            }
        }
        TEST_REQUIRE(!verify_chunk(piece(0), count, bytes, {}, root));
        TEST_REQUIRE(chunk_path(values, count).empty());

        // An index of full blocks gives the same paths, and a missing block fails instead of guessing.
        const auto  levels = full_levels(values);
        std::size_t reads  = 0;
        const auto  read   = [&](std::uint32_t level, std::uint64_t block) -> std::optional<ChainingValue> {
            ++reads;
            if (level >= levels.size() || block >= levels[level].size())
                return std::nullopt;
            return levels[level][block];
        };
        for (std::uint64_t index = 0; index < count; ++index) {
            reads             = 0;
            const auto direct = chunk_path(count, index, read);
            TEST_REQUIRE(direct.has_value() && direct.value() == chunk_path(values, index));
            // At most one full block per level on each side of the path.
            TEST_REQUIRE(reads <= 2 * levels.size() * levels.size());
        }
        TEST_REQUIRE(!chunk_path(count, count, read).has_value());
        if (count > 1)
            TEST_REQUIRE(!chunk_path(count, 0, [](std::uint32_t, std::uint64_t) {
                              return std::optional<ChainingValue> {};
                          }).has_value());
    }
}
