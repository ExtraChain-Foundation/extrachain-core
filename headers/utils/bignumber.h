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
#include <expected>

#include "boost/multiprecision/cpp_int.hpp"
#include "msgpack.hpp"

#include "utils/exc_magic.h"
#include "extrachain_global.h"

#ifdef QT_DEBUG
    #define UPDATE_DEBUG() qdata = to_string();
#else
    #define UPDATE_DEBUG()
#endif

enum class BigNumberError {
    NoError,
    InvalidNumber,
    Infinity
};

/**
 * Arbitrary-precision integer. Canonical string form is decimal.
 * Hex parsing retained for backward compatibility with pre-decimal chain data.
 */
class EXTRACHAIN_EXPORT BigNumber {
public:
    BigNumber();
    explicit BigNumber(const std::string &bigNumber);
    BigNumber(const BigNumber &other);
    BigNumber(BigNumber &&other) noexcept;
    explicit BigNumber(const boost::multiprecision::cpp_int &number);

    template <typename T>
    explicit BigNumber(T number, typename std::enable_if<std::is_integral<T>::value>::type * = nullptr) {
        this->m_data = boost::multiprecision::cpp_int(number);
        UPDATE_DEBUG()
    }

    ~BigNumber() = default;

private:
    boost::multiprecision::cpp_int m_data = 0;

#ifdef QT_DEBUG
    std::string qdata;
#endif

public:
    BigNumber  operator&(const BigNumber &);
    BigNumber  operator>>(const uint &);
    BigNumber  operator>>=(const uint &);
    BigNumber  operator+(const BigNumber &) const;
    BigNumber  operator+(long long) const;
    BigNumber  operator-(const BigNumber &) const;
    BigNumber  operator-(long long) const;
    BigNumber  operator*(const BigNumber &) const;
    BigNumber  operator*(long long) const;
    BigNumber  operator/(const BigNumber &) const;
    BigNumber  operator/(long long) const;
    BigNumber  operator%(const BigNumber &) const;
    BigNumber  operator%(long long) const;
    BigNumber &operator=(const BigNumber &);
    BigNumber &operator=(long long);
    BigNumber &operator++();    // pre increment
    BigNumber  operator++(int); // post increment
    BigNumber &operator--();    // pre increment
    BigNumber  operator--(int); // post increment
    BigNumber &operator+=(const BigNumber &);
    BigNumber &operator+=(long long);
    BigNumber &operator-=(const BigNumber &);
    BigNumber &operator-=(long long);
    BigNumber &operator*=(const BigNumber &);
    BigNumber &operator*=(long long);
    BigNumber &operator/=(const BigNumber &);
    BigNumber &operator/=(long long);
    BigNumber &operator%=(const BigNumber &);
    BigNumber &operator%=(long long);
    BigNumber  operator-() const;

public:
    const boost::multiprecision::cpp_int &data() const;

    bool               is_empty() const;
    std::string        to_string() const;
    std::string        to_hex_string() const;
    std::string        to_printable_string() const;
    std::optional<int> to_int() const;

    BigNumber pow(unsigned long number);
    BigNumber abs() const;

    static std::expected<BigNumber, BigNumberError> create(const std::string &bigNumber);

    // Legacy hex helpers — used by migration and by peers running pre-decimal protocol.
    static bool      is_hex_string(const std::string &str);
    static BigNumber from_hex(const std::string &hex);

    std::strong_ordering operator<=>(const BigNumber &other) const;
    std::strong_ordering operator<=>(const int &other) const;

    bool operator==(const BigNumber &other) const;
    bool operator==(const int &other) const;

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string num = to_string();
        msgpack_pk.pack_str(num.size());
        msgpack_pk.pack_str_body(num.data(), num.size());
    }

    void msgpack_unpack(msgpack::object const &msgpack_o) {
        std::string num = msgpack_o.as<std::string>();
        if (is_hex_string(num)) {
            *this = from_hex(num);
        } else {
            *this = BigNumber(num);
        }
    }
};

MAKE_CUSTOM_MAGICAL(BigNumber)

using SectionId = BigNumber;
