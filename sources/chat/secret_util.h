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

#include "chat/secret_session.h"

#include <cstring>
#include <string_view>

// Byte-level helpers shared by the secret chat sources. Not part of the public interface.
namespace SecretChat::detail {

inline void put_u32(Bytes &out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

inline void put_u64(Bytes &out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

inline void put_bytes(Bytes &out, std::span<const std::uint8_t> data) {
    out.insert(out.end(), data.begin(), data.end());
}

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

    bool take(std::uint8_t *dst, std::size_t n) {
        if (data_.size() - pos_ < n)
            return false;
        std::memcpy(dst, data_.data() + pos_, n);
        pos_ += n;
        return true;
    }
    template <std::size_t N>
    bool take(std::array<std::uint8_t, N> &dst) {
        return take(dst.data(), N);
    }
    bool u8(std::uint8_t &v) { return take(&v, 1); }
    bool u32(std::uint32_t &v) {
        std::array<std::uint8_t, 4> b {};
        if (!take(b))
            return false;
        v = 0;
        for (int i = 0; i < 4; ++i)
            v |= static_cast<std::uint32_t>(b[i]) << (8 * i);
        return true;
    }
    bool u64(std::uint64_t &v) {
        std::array<std::uint8_t, 8> b {};
        if (!take(b))
            return false;
        v = 0;
        for (int i = 0; i < 8; ++i)
            v |= static_cast<std::uint64_t>(b[i]) << (8 * i);
        return true;
    }
    std::size_t remaining() const { return data_.size() - pos_; }
    std::span<const std::uint8_t> rest() const { return data_.subspan(pos_); }

private:
    std::span<const std::uint8_t> data_;
    std::size_t                   pos_ = 0;
};

inline bool utf8_valid(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<std::uint8_t>(s[i]);
        std::size_t n = 0;
        std::uint32_t cp = 0;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            n  = 1;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            n  = 2;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            n  = 3;
            cp = c & 0x07;
        } else {
            return false;
        }
        if (i + n >= s.size())
            return false;
        for (std::size_t k = 1; k <= n; ++k) {
            const auto cc = static_cast<std::uint8_t>(s[i + k]);
            if ((cc & 0xC0) != 0x80)
                return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000) || cp > 0x10FFFF
            || (cp >= 0xD800 && cp <= 0xDFFF))
            return false;
        i += n + 1;
    }
    return true;
}

} // namespace SecretChat::detail
