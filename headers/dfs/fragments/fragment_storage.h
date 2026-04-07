#pragma once

#include "dfs/fragments/merkle.h"
#include "chain/actor_id.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class FsPath;

namespace Dfs::Fragments {

static constexpr uint32_t STORAGE_VERSION = 1;
static constexpr std::string_view HASH_PREFIX = "fg:";

enum class StorageError {
    FileNotFound,
    ReadError,
    WriteError,
    VersionMismatch,
    InvalidFormat,
};

struct FragmentsFile {
    uint32_t            version        = STORAGE_VERSION;
    uint32_t            fragment_size  = MERKLE_LEAF_SIZE;
    uint32_t            fragment_count = 0;
    Hash32              merkle_root {};
    std::vector<Hash32> leaves;
};

// Hex conversion (64-char lowercase hex <-> 32-byte array)
std::string to_hex(const Hash32& hash);
std::expected<Hash32, StorageError> from_hex(const std::string& hex);

// Check if DirRow::hash has fg: prefix
bool is_fragment_hash(const std::string& hash);

// Extract merkle root hex from "fg:abcdef..." -> "abcdef..."
std::string parse_merkle_root_hex(const std::string& hash);

// Single-pass file hashing: computes flat Blake3 + Merkle leaf hashes simultaneously
// Returns {flat_blake3_hex_64chars, leaf_hashes}
std::pair<std::string, std::vector<Hash32>> hash_file(const FsPath& path);

// Path for .fragments file: dfs/<owner>/<file_id>.fragments
std::filesystem::path make_path(const ActorId& owner_id, const std::string& file_id);
bool exists(const ActorId& owner_id, const std::string& file_id);

// Binary I/O for .fragments
std::expected<void, StorageError> write(const std::filesystem::path& path, const FragmentsFile& file);
std::expected<FragmentsFile, StorageError> read(const std::filesystem::path& path);

// .partial file — bitmap of downloaded network fragments
// Format: [4 bytes total_fragments] [ceil(N/8) bytes bitmap]
std::filesystem::path make_partial_path(const ActorId& owner_id, const std::string& file_id);
bool partial_exists(const ActorId& owner_id, const std::string& file_id);
std::expected<void, StorageError> write_partial(const std::filesystem::path& path,
                                                uint32_t total_fragments,
                                                const std::set<size_t>& achieved);
std::expected<std::set<size_t>, StorageError> read_partial(const std::filesystem::path& path);
void remove_partial(const ActorId& owner_id, const std::string& file_id);

} // namespace Dfs::Fragments
