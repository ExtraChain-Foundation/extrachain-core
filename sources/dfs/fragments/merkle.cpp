#include "dfs/fragments/merkle.h"

#include <cstring>

namespace Dfs::Fragments {

Hash32 hash_leaf(const uint8_t* data, size_t size) noexcept {
    Hash32 result {};
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, size);
    blake3_hasher_finalize(&hasher, result.data(), BLAKE3_OUT_LEN);
    return result;
}

Hash32 hash_node(const Hash32& left, const Hash32& right) noexcept {
    Hash32 result {};
    uint8_t combined[BLAKE3_OUT_LEN * 2];
    std::memcpy(combined, left.data(), BLAKE3_OUT_LEN);
    std::memcpy(combined + BLAKE3_OUT_LEN, right.data(), BLAKE3_OUT_LEN);

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, combined, sizeof(combined));
    blake3_hasher_finalize(&hasher, result.data(), BLAKE3_OUT_LEN);
    return result;
}

Hash32 compute_root(const std::vector<Hash32>& leaves) {
    if (leaves.empty()) {
        return Hash32 {};
    }

    if (leaves.size() == 1) {
        return leaves[0];
    }

    std::vector<Hash32> layer = leaves;

    while (layer.size() > 1) {
        if (layer.size() % 2 != 0) {
            layer.push_back(layer.back());
        }

        std::vector<Hash32> next;
        next.reserve(layer.size() / 2);

        for (size_t i = 0; i < layer.size(); i += 2) {
            next.push_back(hash_node(layer[i], layer[i + 1]));
        }

        layer = std::move(next);
    }

    return layer[0];
}

bool verify_leaf(const Hash32& expected_hash, const uint8_t* data, size_t size) noexcept {
    Hash32 actual = hash_leaf(data, size);
    return actual == expected_hash;
}

} // namespace Dfs::Fragments
