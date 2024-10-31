/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

#ifndef BIGNUMBER_H
#define BIGNUMBER_H

#include <QDebug>
#include <QMetaType>
#include <QRandomGenerator>
#include <QString>
#include <QtCore/QChar>
#include <QtCore/QString>
#include <sstream>
#include <string>
#include <expected>

#include "boost/multiprecision/cpp_int.hpp"
#include "msgpack.hpp"
#include "utils/exc_magic.h"

#include "extrachain_global.h"

#ifdef QT_DEBUG
    #define UPDATE_DEBUG()                                                                                   \
        qdata    = toStdString(NumeralBase::Hex);                                                            \
        qdataDec = toStdString(NumeralBase::Dec);
#else
    #define UPDATE_DEBUG()
#endif

enum class NumeralBase {
    Dec = 10,
    Hex = 16
};

namespace BigNumberUtils {
const static QList<char> Chars = { 'a', 'b', 'c', 'd', 'e', 'f', '0', '1',
                                   '2', '3', '4', '5', '6', '7', '8', '9' };
}

enum class BigNumberError {
    NoError,
    InvalidNumber,
    Infinity
};

/**
 * Data type for big hex numbers for addresses
 * example: ab11405c92a05c91c48
 */
class EXTRACHAIN_EXPORT BigNumber {
public:
    BigNumber();
    explicit BigNumber(const std::string &bigNumber, NumeralBase base = NumeralBase::Hex);
    BigNumber(const BigNumber &other);
    BigNumber(BigNumber &&other) noexcept;
    explicit BigNumber(int number);
    explicit BigNumber(long long number);
    explicit BigNumber(const boost::multiprecision::cpp_int &number);
    ~BigNumber() = default;

private:
    boost::multiprecision::cpp_int m_data = 0;

#ifdef QT_DEBUG
    std::string qdata;
    std::string qdataDec;
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
    bool                                  isEmpty() const;
    QByteArray                            toByteArray(NumeralBase numSystem = NumeralBase::Hex) const;
    std::string                           toStdString(NumeralBase numSystem = NumeralBase::Hex) const;
    std::string                           toZeroStdString(int size) const;
    BigNumber                             pow(unsigned long number);
    // BigNumber sqrt(unsigned long number = 2) const;
    BigNumber abs() const;

    static std::expected<BigNumber, BigNumberError>
                     create(const std::string &bigNumber, NumeralBase base = NumeralBase::Hex);
    static BigNumber random(int n, bool zeroAllowed = true);
    static BigNumber random(int n, const BigNumber &max, bool zeroAllowed = true);
    static BigNumber random(BigNumber max, bool zeroAllowed = true);

    std::strong_ordering operator<=>(const BigNumber &other) const {
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

    bool operator==(const BigNumber &other) const {
        return m_data == other.m_data;
    }

    bool operator==(const int &other) const {
        return m_data == other;
    }

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string num = toStdString();
        msgpack_pk.pack_str(num.size());
        msgpack_pk.pack_str_body(num.data(), num.size());
    }

    void msgpack_unpack(msgpack::object const &msgpack_o) {
        std::string num = msgpack_o.as<std::string>();
        *this           = BigNumber(num);
    }
};

inline size_t qHash(const BigNumber &key, size_t seed) {
    return qHash(key, seed);
}

MAKE_CUSTOM_MAGICAL(BigNumber)

#endif // BIGNUMBER_H
