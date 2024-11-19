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

#include <string>
#include "cpp-base64/base64.h"

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
Container from_base64(const std::string &input) {
    std::string base64 = input;

    for (char &c : base64) {
        if (c == '-')
            c = '+';
        else if (c == '_')
            c = '/';
    }

    int padding = (4 - (base64.size() % 4)) % 4;
    base64.append(padding, '=');

    std::string decoded = base64_decode(base64);

    if constexpr (std::is_same_v<Container, std::string>) {
        return decoded;
    }

    return Container(decoded.begin(), decoded.end());
}
} // namespace Utils
