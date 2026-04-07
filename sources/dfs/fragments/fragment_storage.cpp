#include "dfs/fragments/fragment_storage.h"

#include "dfs/dfs_utils.h"
#include "utils/fs_path.h"

#include <cstdio>
#include <cstring>
namespace Dfs::Fragments {

std::string to_hex(const Hash32& hash) {
    return fmt::format("{:02x}", fmt::join(std::span(hash.data(), BLAKE3_OUT_LEN), ""));
}

std::expected<Hash32, StorageError> from_hex(const std::string& hex) {
    auto bytes = Utils::from_hex(hex);
    if (!bytes.has_value()) {
        return std::unexpected(StorageError::InvalidFormat);
    }

    if (bytes->size() != BLAKE3_OUT_LEN) {
        return std::unexpected(StorageError::InvalidFormat);
    }

    Hash32 result {};
    std::memcpy(result.data(), bytes->data(), BLAKE3_OUT_LEN);
    return result;
}

bool is_fragment_hash(const std::string& hash) {
    return hash.starts_with(HASH_PREFIX);
}

std::string parse_merkle_root_hex(const std::string& hash) {
    if (!is_fragment_hash(hash)) {
        return {};
    }
    return hash.substr(HASH_PREFIX.size());
}

std::pair<std::string, std::vector<Hash32>> hash_file(const FsPath& path) {
    blake3_hasher flat_hasher;
    blake3_hasher_init(&flat_hasher);

    blake3_hasher chunk_hasher;
    blake3_hasher_init(&chunk_hasher);

    std::vector<Hash32> leaves;
    size_t leaf_pos = 0;

    FILE* f = fopen(path.native().string().c_str(), "rb");
    if (!f) {
        return {};
    }

    constexpr size_t BUF_SIZE = 65536;
    std::array<uint8_t, BUF_SIZE> read_buf;
    size_t n;

    while ((n = fread(read_buf.data(), 1, BUF_SIZE, f)) > 0) {
        blake3_hasher_update(&flat_hasher, read_buf.data(), n);

        size_t src_pos = 0;
        while (src_pos < n) {
            size_t space = MERKLE_LEAF_SIZE - leaf_pos;
            size_t to_copy = std::min(space, n - src_pos);
            blake3_hasher_update(&chunk_hasher, read_buf.data() + src_pos, to_copy);
            leaf_pos += to_copy;
            src_pos += to_copy;

            if (leaf_pos == MERKLE_LEAF_SIZE) {
                Hash32 leaf_hash {};
                blake3_hasher_finalize(&chunk_hasher, leaf_hash.data(), BLAKE3_OUT_LEN);
                leaves.push_back(leaf_hash);
                blake3_hasher_init(&chunk_hasher);
                leaf_pos = 0;
            }
        }
    }

    fclose(f);

    if (leaf_pos > 0) {
        Hash32 leaf_hash {};
        blake3_hasher_finalize(&chunk_hasher, leaf_hash.data(), BLAKE3_OUT_LEN);
        leaves.push_back(leaf_hash);
    }

    uint8_t flat_output[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&flat_hasher, flat_output, BLAKE3_OUT_LEN);
    std::string flat_hex = fmt::format("{:02x}", fmt::join(std::span(flat_output, BLAKE3_OUT_LEN), ""));

    return { std::move(flat_hex), std::move(leaves) };
}

std::filesystem::path make_path(const ActorId& owner_id, const std::string& file_id) {
    return fmt::format("{}/{}/{}.fragments", Dfs::Basic::DFS_FOLDER, owner_id, file_id);
}

bool exists(const ActorId& owner_id, const std::string& file_id) {
    return std::filesystem::exists(make_path(owner_id, file_id));
}

std::expected<void, StorageError> write(const std::filesystem::path& path, const FragmentsFile& file) {
    FILE* f = fopen(path.string().c_str(), "wb");
    if (!f) {
        return std::unexpected(StorageError::WriteError);
    }

    if (fwrite(&file.version, sizeof(uint32_t), 1, f) != 1
        || fwrite(&file.fragment_size, sizeof(uint32_t), 1, f) != 1
        || fwrite(&file.fragment_count, sizeof(uint32_t), 1, f) != 1
        || fwrite(file.merkle_root.data(), BLAKE3_OUT_LEN, 1, f) != 1) {
        fclose(f);
        return std::unexpected(StorageError::WriteError);
    }

    for (const auto& leaf : file.leaves) {
        if (fwrite(leaf.data(), BLAKE3_OUT_LEN, 1, f) != 1) {
            fclose(f);
            return std::unexpected(StorageError::WriteError);
        }
    }

    fclose(f);
    return {};
}

std::expected<FragmentsFile, StorageError> read(const std::filesystem::path& path) {
    FILE* f = fopen(path.string().c_str(), "rb");
    if (!f) {
        return std::unexpected(StorageError::FileNotFound);
    }

    FragmentsFile result;

    if (fread(&result.version, sizeof(uint32_t), 1, f) != 1
        || fread(&result.fragment_size, sizeof(uint32_t), 1, f) != 1
        || fread(&result.fragment_count, sizeof(uint32_t), 1, f) != 1
        || fread(result.merkle_root.data(), BLAKE3_OUT_LEN, 1, f) != 1) {
        fclose(f);
        return std::unexpected(StorageError::ReadError);
    }

    if (result.version != STORAGE_VERSION) {
        fclose(f);
        return std::unexpected(StorageError::VersionMismatch);
    }

    result.leaves.resize(result.fragment_count);
    for (uint32_t i = 0; i < result.fragment_count; ++i) {
        if (fread(result.leaves[i].data(), BLAKE3_OUT_LEN, 1, f) != 1) {
            fclose(f);
            return std::unexpected(StorageError::ReadError);
        }
    }

    fclose(f);
    return result;
}

std::filesystem::path make_partial_path(const ActorId& owner_id, const std::string& file_id) {
    return fmt::format("{}/{}/{}.partial", Dfs::Basic::DFS_FOLDER, owner_id, file_id);
}

bool partial_exists(const ActorId& owner_id, const std::string& file_id) {
    return std::filesystem::exists(make_partial_path(owner_id, file_id));
}

std::expected<void, StorageError> write_partial(const std::filesystem::path& path,
                                                uint32_t total_fragments,
                                                const std::set<size_t>& achieved) {
    FILE* f = fopen(path.string().c_str(), "wb");
    if (!f) {
        return std::unexpected(StorageError::WriteError);
    }

    if (fwrite(&total_fragments, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        return std::unexpected(StorageError::WriteError);
    }

    size_t bitmap_size = (total_fragments + 7) / 8;
    std::vector<uint8_t> bitmap(bitmap_size, 0);
    for (size_t frag : achieved) {
        if (frag >= 1 && frag <= total_fragments) {
            size_t idx = frag - 1; // 1-based to 0-based
            bitmap[idx / 8] |= (1 << (idx % 8));
        }
    }

    if (fwrite(bitmap.data(), 1, bitmap_size, f) != bitmap_size) {
        fclose(f);
        return std::unexpected(StorageError::WriteError);
    }

    fclose(f);
    return {};
}

std::expected<std::set<size_t>, StorageError> read_partial(const std::filesystem::path& path) {
    FILE* f = fopen(path.string().c_str(), "rb");
    if (!f) {
        return std::unexpected(StorageError::FileNotFound);
    }

    uint32_t total_fragments;
    if (fread(&total_fragments, sizeof(uint32_t), 1, f) != 1) {
        fclose(f);
        return std::unexpected(StorageError::ReadError);
    }

    size_t bitmap_size = (total_fragments + 7) / 8;
    std::vector<uint8_t> bitmap(bitmap_size, 0);
    if (fread(bitmap.data(), 1, bitmap_size, f) != bitmap_size) {
        fclose(f);
        return std::unexpected(StorageError::ReadError);
    }

    fclose(f);

    std::set<size_t> achieved;
    for (uint32_t i = 0; i < total_fragments; ++i) {
        if (bitmap[i / 8] & (1 << (i % 8))) {
            achieved.insert(i + 1); // 0-based to 1-based
        }
    }

    return achieved;
}

void remove_partial(const ActorId& owner_id, const std::string& file_id) {
    auto path = make_partial_path(owner_id, file_id);
    std::filesystem::remove(path);
}

} // namespace Dfs::Fragments
