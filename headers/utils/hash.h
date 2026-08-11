#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>

#include <blake3.h>
#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <fmt/format.h>

#include "extrachain_global.h"
#include "utils/exc_magic.h"
#include "utils/fs_path.h"

namespace Utils {
    enum class HashAlgorithm {
        Blake3
    };

    EXTRACHAIN_EXPORT std::string calculate_hash(
        const std::string &data,
        HashAlgorithm      hash_algorithm = HashAlgorithm::Blake3);

    EXTRACHAIN_EXPORT std::string calculate_hash_bytes(
        const char   *data,
        std::size_t   size,
        HashAlgorithm hash_algorithm = HashAlgorithm::Blake3);

    namespace detail {
        template <typename T>
        void update_hasher(blake3_hasher &hasher, const T &value) {
            if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view>) {
                blake3_hasher_update(&hasher, value.data(), value.size());
            } else if constexpr (std::is_arithmetic_v<T>) {
                const auto text = std::to_string(value);
                blake3_hasher_update(&hasher, text.data(), text.size());
            } else if constexpr (std::is_enum_v<T>) {
                const auto text = std::to_string(static_cast<std::underlying_type_t<T>>(value));
                blake3_hasher_update(&hasher, text.data(), text.size());
            } else if constexpr (magic::is_optional<T>::value) {
                if (value.has_value()) {
                    update_hasher(hasher, value.value());
                }
            } else {
                const auto text = magic::detail::to_string(value);
                blake3_hasher_update(&hasher, text.data(), text.size());
            }
        }
    } // namespace detail

    template <typename T>
    std::string calculate_hash_blake3(const T &value) {
        blake3_hasher hasher;
        blake3_hasher_init(&hasher);

        if constexpr (boost::describe::has_describe_members<T>::value) {
            boost::mp11::mp_for_each<boost::describe::describe_members<T,
                                                                       boost::describe::mod_any_access
                                                                           | boost::describe::mod_inherited>>(
                [&](auto descriptor) {
                    if constexpr (!std::is_same_v<decltype(descriptor), magic::custom_magic_tag>) {
                        const auto field_name = magic::detail::clean_type_name(descriptor.name);
                        if (field_name != "sign" && field_name != "signature" && field_name != "hash") {
                            detail::update_hasher(hasher, magic::invoke_member(value, descriptor.pointer));
                        }
                    }
                });
        } else {
            detail::update_hasher(hasher, value);
        }

        std::uint8_t output[BLAKE3_OUT_LEN];
        blake3_hasher_finalize(&hasher, output, BLAKE3_OUT_LEN);

        std::string hash;
        hash.reserve(BLAKE3_OUT_LEN * 2);
        for (const std::uint8_t byte : output) {
            hash += fmt::format("{:02x}", byte);
        }
        return hash;
    }

    template <typename T>
    std::string calculate_hash(const T &value, HashAlgorithm hash_algorithm = HashAlgorithm::Blake3) {
        switch (hash_algorithm) {
        case HashAlgorithm::Blake3:
            return calculate_hash_blake3(value);
        }
        return {};
    }

    enum class FileHashError {
        FileNotFound,
        ReadError,
        HashError,
        AccessError
    };

    EXTRACHAIN_EXPORT std::expected<std::string, FileHashError> calculate_hash_file(const FsPath &path);
} // namespace Utils
