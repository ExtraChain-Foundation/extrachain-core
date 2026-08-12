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

#include "cpp-base64/base64.h"

enum class Base64Error {
    InvalidPadding,
    InvalidInput,
    DecodingError
};

namespace Utils {
    template <typename Container>
    std::string to_base64(const Container &input) {
        std::string result = base64_encode(reinterpret_cast<const unsigned char *>(input.data()), input.size());

        for (char &c : result) {
            if (c == '+')
                c = '-';
            else if (c == '/')
                c = '_';
            else if (c == '=') {
                result.resize(result.find('='));
                break;
            }
        }

        return result;
    }

    template <typename Container = std::string>
    std::expected<Container, Base64Error> from_base64(const std::string &input) {
        try {
            const auto padding = input.find('=');
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

            std::string base64 = input;
            for (char &c : base64) {
                if (c == '-')
                    c = '+';
                else if (c == '_')
                    c = '/';
            }

            if (base64.size() % 4 != 0) {
                int padding = (4 - (base64.size() % 4)) % 4;
                base64.append(padding, '=');
            }

            std::string decoded;
            try {
                decoded = base64_decode(base64);
            } catch (...) {
                return std::unexpected(Base64Error::DecodingError);
            }

            std::string canonical_input = input.substr(0, encoded_size);
            std::ranges::replace(canonical_input, '+', '-');
            std::ranges::replace(canonical_input, '/', '_');
            if (to_base64(decoded) != canonical_input) {
                return std::unexpected(Base64Error::InvalidPadding);
            }

            if constexpr (std::is_same_v<Container, std::string>) {
                return decoded;
            }
            return Container(decoded.begin(), decoded.end());
        } catch (...) {
            return std::unexpected(Base64Error::InvalidInput);
        }
    }
} // namespace Utils
