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

#include "boost/multiprecision/cpp_dec_float.hpp"
#include "msgpack.hpp"

#include "extrachain_global.h"
#include "utils/bignumber.h"

const int float_size    = 60;
using cpp_dec_float_exc = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<float_size>>;

/**
 * Data type for big decimal numbers
 * example: 12345.678901234567890
 */
class EXTRACHAIN_EXPORT BigNumberFloat {
public:
    BigNumberFloat();
    explicit BigNumberFloat(const std::string &bigNumberFloat);
    BigNumberFloat(const BigNumberFloat &other);
    BigNumberFloat(BigNumberFloat &&other) noexcept;
    explicit BigNumberFloat(const BigNumber &other);
    explicit BigNumberFloat(int number);
    explicit BigNumberFloat(long long number);
    explicit BigNumberFloat(std::uint64_t number);
    explicit BigNumberFloat(const cpp_dec_float_exc &number);
    ~BigNumberFloat() = default;

private:
    cpp_dec_float_exc m_data = 0;

#ifdef QT_DEBUG
    std::string qdata;
#endif

public:
    BigNumberFloat  operator+(const BigNumberFloat &) const;
    BigNumberFloat  operator+(long long) const;
    BigNumberFloat  operator-(const BigNumberFloat &) const;
    BigNumberFloat  operator-(long long) const;
    BigNumberFloat  operator*(const BigNumberFloat &) const;
    BigNumberFloat  operator*(long long) const;
    BigNumberFloat  operator/(const BigNumberFloat &) const;
    BigNumberFloat  operator/(long long) const;
    BigNumberFloat &operator=(const BigNumberFloat &);
    BigNumberFloat &operator=(long long);
    BigNumberFloat &operator++();    // pre increment
    BigNumberFloat  operator++(int); // post increment
    BigNumberFloat &operator--();    // pre increment
    BigNumberFloat  operator--(int); // post increment
    BigNumberFloat &operator+=(const BigNumberFloat &);
    BigNumberFloat &operator+=(long long);
    BigNumberFloat &operator-=(const BigNumberFloat &);
    BigNumberFloat &operator-=(long long);
    BigNumberFloat &operator*=(const BigNumberFloat &);
    BigNumberFloat &operator*=(long long);
    BigNumberFloat &operator/=(const BigNumberFloat &);
    BigNumberFloat &operator/=(long long);
    BigNumberFloat  operator-() const;

public:
    const cpp_dec_float_exc &data() const;
    std::string              to_string() const;
    std::string              to_hex_string() const;
    BigNumberFloat           pow(unsigned long number);
    // BigNumberFloat sqrt(unsigned long number = 2) const;
    BigNumberFloat abs() const;

    static std::expected<BigNumberFloat, BigNumberError> create(const std::string &bigNumberFloat);

    void truncate(int decimalPlaces = 3);

    std::strong_ordering operator<=>(const BigNumberFloat &other) const;
    std::strong_ordering operator<=>(const int &other) const;
    bool                 operator==(const BigNumberFloat &other) const;
    bool                 operator==(const int &other) const;
    bool                 operator!=(const BigNumberFloat &other) const;
    bool                 operator!=(const int &other) const;

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string num = to_string();
        msgpack_pk.pack_str(num.size());
        msgpack_pk.pack_str_body(num.data(), num.size());
    }

    void msgpack_unpack(msgpack::object const &msgpack_o) {
        std::string num = msgpack_o.as<std::string>();
        *this           = BigNumberFloat(num);
    }
};

inline size_t qHash(const BigNumberFloat &key, size_t seed) {
    return qHash(key, seed);
}

MAKE_CUSTOM_MAGICAL(BigNumberFloat)
