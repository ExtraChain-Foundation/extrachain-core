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

#include <algorithm>
#include <expected>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <sodium.h>

enum class Base64Error {
    InvalidPadding,
    InvalidInput,
    DecodingError
};

namespace Utils {
    template <typename Container>
    std::string to_base64(const Container &input) {
        const auto encoded_size =
            sodium_base64_encoded_len(input.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
        std::string result(encoded_size, '\0');
        sodium_bin2base64(result.data(),
                          result.size(),
                          reinterpret_cast<const unsigned char *>(input.data()),
                          input.size(),
                          sodium_base64_VARIANT_URLSAFE_NO_PADDING);
        if (!result.empty()) {
            result.resize(result.size() - 1);
        }
        return result;
    }

    template <typename Container = std::string>
    std::expected<Container, Base64Error> from_base64(const std::string &input) {
        try {
            if (input.empty()) {
                return Container {};
            }
            const std::string_view encoded(input);
            const auto             padding      = encoded.find('=');
            const auto             encoded_size = padding == std::string::npos ? input.size() : padding;
            if (encoded_size % 4 == 1) {
                return std::unexpected(Base64Error::InvalidPadding);
            }
            if (padding != std::string::npos
                && (input.size() % 4 != 0 || input.size() - padding > 2
                    || !std::ranges::all_of(encoded.substr(padding), [](char character) {
                           return character == '=';
                       }))) {
                return std::unexpected(Base64Error::InvalidPadding);
            }
            if (!std::ranges::all_of(encoded.substr(0, encoded_size), [](unsigned char character) {
                    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                           || (character >= '0' && character <= '9') || character == '+' || character == '/'
                           || character == '-' || character == '_';
                })) {
                return std::unexpected(Base64Error::InvalidInput);
            }

            auto        canonical_input = encoded.substr(0, encoded_size);
            std::string normalized;
            if (canonical_input.find_first_of("+/") != std::string_view::npos) {
                normalized.assign(canonical_input);
                for (char &character : normalized) {
                    if (character == '+') {
                        character = '-';
                    } else if (character == '/') {
                        character = '_';
                    }
                }
                canonical_input = normalized;
            }

            using Buffer = std::conditional_t<std::is_same_v<Container, std::string>
                                                  || std::is_same_v<Container, std::vector<unsigned char>>
                                                  || std::is_same_v<Container, std::vector<char>>,
                                              Container,
                                              std::vector<unsigned char>>;
            Buffer decoded;
            decoded.resize((encoded_size / 4) * 3 + (encoded_size % 4) * 3 / 4);
            std::size_t decoded_size = 0;
            // libsodium rejects nonzero trailing bits as part of decoding.
            if (sodium_base642bin(reinterpret_cast<unsigned char *>(decoded.data()),
                                  decoded.size(),
                                  canonical_input.data(),
                                  canonical_input.size(),
                                  nullptr,
                                  &decoded_size,
                                  nullptr,
                                  sodium_base64_VARIANT_URLSAFE_NO_PADDING)
                != 0) {
                return std::unexpected(Base64Error::DecodingError);
            }
            decoded.resize(decoded_size);

            if constexpr (std::is_same_v<Container, Buffer>) {
                return decoded;
            } else {
                return Container(decoded.begin(), decoded.end());
            }
        } catch (...) {
            return std::unexpected(Base64Error::InvalidInput);
        }
    }
} // namespace Utils
