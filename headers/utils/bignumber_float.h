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

const int float_size    = 100;
using cpp_dec_float_exc = boost::multiprecision::number<boost::multiprecision::cpp_dec_float<float_size>>;

/**
 * Data type for big hex numbers for addresses
 * example: ab11405c92a05c91c48
 */
class EXTRACHAIN_EXPORT BigNumberFloat {
public:
    BigNumberFloat();
    explicit BigNumberFloat(const std::string &bigNumberFloat, NumeralBase base = NumeralBase::Hex);
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
    std::string qdataDec;
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
    std::string              to_string(NumeralBase base = NumeralBase::Hex) const;
    BigNumberFloat           pow(unsigned long number);
    // BigNumberFloat sqrt(unsigned long number = 2) const;
    BigNumberFloat abs() const;

    static std::expected<BigNumberFloat, BigNumberError>
                          create(const std::string &bigNumberFloat, NumeralBase base = NumeralBase::Hex);
    static BigNumberFloat from_hex(const std::string &number);

    std::strong_ordering operator<=>(const BigNumberFloat &other) const {
        if (m_data < other.m_data)
            return std::strong_ordering::less;
        if (m_data > other.m_data)
            return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }

    std::strong_ordering operator<=>(const int &other) const {
        if (m_data < other)
            return std::strong_ordering::less;
        if (m_data > other)
            return std::strong_ordering::greater;
        return std::strong_ordering::equal;
    }

    bool operator==(const BigNumberFloat &other) const {
        return m_data == other.m_data;
    }

    bool operator==(const int &other) const {
        return m_data == other;
    }

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
