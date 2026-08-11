#include "utils/hash.h"

#include <cstdio>
#include <span>
#include <vector>

#include <fmt/ranges.h>

std::string Utils::calculate_hash_bytes(const char *data, std::size_t size, HashAlgorithm hash_algorithm) {
    if (hash_algorithm != HashAlgorithm::Blake3) {
        return {};
    }

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, size);

    std::uint8_t hash[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);
    return fmt::format("{:02x}", fmt::join(std::span(hash, BLAKE3_OUT_LEN), ""));
}

std::string Utils::calculate_hash(const std::string &data, HashAlgorithm hash_algorithm) {
    return calculate_hash_bytes(data.data(), data.size(), hash_algorithm);
}

std::expected<std::string, Utils::FileHashError> Utils::calculate_hash_file(const FsPath &path) {
    const auto exists = path.exists();
    if (!exists) {
        return std::unexpected(FileHashError::FileNotFound);
    }

    const auto has_read_permission = path.has_read_permission();
    if (!has_read_permission || !*has_read_permission) {
        return std::unexpected(FileHashError::AccessError);
    }

    FILE *file = std::fopen(path.native().string().c_str(), "rb");
    if (file == nullptr) {
        return std::unexpected(FileHashError::ReadError);
    }

    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    constexpr std::size_t BufferSize = 64 * 1024;
    std::vector<std::uint8_t> buffer(BufferSize);
    while (const std::size_t bytes_read = std::fread(buffer.data(), 1, buffer.size(), file)) {
        blake3_hasher_update(&hasher, buffer.data(), bytes_read);
    }

    if (std::ferror(file) != 0) {
        std::fclose(file);
        return std::unexpected(FileHashError::ReadError);
    }
    std::fclose(file);

    std::uint8_t hash[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);
    return fmt::format("{:02x}", fmt::join(std::span(hash, BLAKE3_OUT_LEN), ""));
}
