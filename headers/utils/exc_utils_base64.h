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
            const auto padding      = input.find('=');
            const auto encoded_size = padding == std::string::npos ? input.size() : padding;
            if (encoded_size % 4 == 1) {
                return std::unexpected(Base64Error::InvalidPadding);
            }
            if (padding != std::string::npos
                && (input.size() % 4 != 0 || input.size() - padding > 2
                    || !std::ranges::all_of(input.substr(padding), [](char character) {
                           return character == '=';
                       }))) {
                return std::unexpected(Base64Error::InvalidPadding);
            }
            if (!std::ranges::all_of(input.substr(0, encoded_size), [](unsigned char character) {
                    return (character >= 'A' && character <= 'Z') || (character >= 'a' && character <= 'z')
                           || (character >= '0' && character <= '9') || character == '+' || character == '/'
                           || character == '-' || character == '_';
                })) {
                return std::unexpected(Base64Error::InvalidInput);
            }

            std::string canonical_input = input.substr(0, encoded_size);
            for (char &character : canonical_input) {
                if (character == '+') {
                    character = '-';
                } else if (character == '/') {
                    character = '_';
                }
            }

            std::vector<unsigned char> decoded(canonical_input.size());
            std::size_t                decoded_size = 0;
            if (sodium_base642bin(decoded.data(),
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

            if (to_base64(decoded) != canonical_input) {
                return std::unexpected(Base64Error::InvalidPadding);
            }

            if constexpr (std::is_same_v<Container, std::string>) {
                return std::string(decoded.begin(), decoded.end());
            }
            return Container(decoded.begin(), decoded.end());
        } catch (...) {
            return std::unexpected(Base64Error::InvalidInput);
        }
    }
} // namespace Utils
