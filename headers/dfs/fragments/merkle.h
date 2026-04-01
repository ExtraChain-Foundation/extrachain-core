#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <blake3.h>

namespace Dfs::Fragments {

using Hash32 = std::array<uint8_t, BLAKE3_OUT_LEN>;

static constexpr uint32_t MERKLE_LEAF_SIZE = 512000; // 512KB

// Blake3 hash of raw leaf data (up to MERKLE_LEAF_SIZE bytes)
Hash32 hash_leaf(const uint8_t* data, size_t size) noexcept;

// Blake3(left || right) — internal Merkle node hash
Hash32 hash_node(const Hash32& left, const Hash32& right) noexcept;

// Build Merkle root from leaf hashes
// Odd leaf is duplicated before pairing
// Empty input returns zero-filled Hash32
Hash32 compute_root(const std::vector<Hash32>& leaves);

// Verify that data hashes to expected_hash
bool verify_leaf(const Hash32& expected_hash, const uint8_t* data, size_t size) noexcept;

} // namespace Dfs::Fragments
