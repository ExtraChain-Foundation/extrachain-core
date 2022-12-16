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

#ifndef BIGNUMBER_FLOAT_H
#define BIGNUMBER_FLOAT_H

#include <QDebug>
#include <QMetaType>
#include <QRandomGenerator>
#include <QString>
#include <QtCore/QChar>
#include <QtCore/QString>
#include <sstream>
#include <string>

#include "boost/multiprecision/cpp_bin_float.hpp"
#include "msgpack.hpp"

#include "extrachain_global.h"
#include "utils/bignumber.h"

#ifdef QT_DEBUG
    #define UPDATE_DEBUG()       \
        qdata = toStdString(16); \
        qdataDec = toStdString(10);
#else
    #define UPDATE_DEBUG()
#endif

// namespace BigNumberUtils {
// const static QList<char> Chars = { 'a', 'b', 'c', 'd', 'e', 'f', '0', '1',
//                                    '2', '3', '4', '5', '6', '7', '8', '9' };
// }

/**
 * Data type for big hex numbers for addresses
 * example: ab11405c92a05c91c48
 */
class EXTRACHAIN_EXPORT BigNumberFloat {
public:
    BigNumberFloat();
    BigNumberFloat(const std::string &bigNumberFloat, int base = 16);
    BigNumberFloat(const BigNumberFloat &other);
    BigNumberFloat(const BigNumber &other);
    BigNumberFloat(int number);
    BigNumberFloat(long long number);
    BigNumberFloat(uint64_t number);
    BigNumberFloat(const boost::multiprecision::cpp_bin_float_50 &number);
    ~BigNumberFloat() = default;

private:
    boost::multiprecision::cpp_bin_float_50 m_data;

#ifdef QT_DEBUG
    std::string qdata;
    std::string qdataDec;
#endif

public:
    BigNumberFloat operator+(const BigNumberFloat &);
    BigNumberFloat operator+(long long);
    BigNumberFloat operator-(const BigNumberFloat &);
    BigNumberFloat operator-(long long);
    BigNumberFloat operator*(const BigNumberFloat &) const;
    BigNumberFloat operator*(long long);
    BigNumberFloat operator/(const BigNumberFloat &);
    BigNumberFloat operator/(long long);
    BigNumberFloat &operator=(const BigNumberFloat &);
    BigNumberFloat &operator=(long long);
    BigNumberFloat &operator++();   // pre increment
    BigNumberFloat operator++(int); // post increment
    BigNumberFloat &operator--();   // pre increment
    BigNumberFloat operator--(int); // post increment
    BigNumberFloat &operator+=(const BigNumberFloat &);
    BigNumberFloat &operator+=(long long);
    BigNumberFloat &operator-=(const BigNumberFloat &);
    BigNumberFloat &operator-=(long long);
    BigNumberFloat &operator*=(const BigNumberFloat &);
    BigNumberFloat &operator*=(long long);
    BigNumberFloat &operator/=(const BigNumberFloat &);
    BigNumberFloat &operator/=(long long);
    BigNumberFloat operator-() const;

public:
    const boost::multiprecision::cpp_bin_float_50 &data() const;
    bool isEmpty() const;
    QByteArray toByteArray(int base = 16) const;
    std::string toStdString(int base = 16) const;
    QByteArray toZeroByteArray(int size) const;
    BigNumberFloat pow(unsigned long number);
    // BigNumberFloat sqrt(unsigned long number = 2) const;
    BigNumberFloat abs() const;
    static BigNumberFloat random(int n, bool zeroAllowed = true);
    static BigNumberFloat random(int n, const BigNumberFloat &max, bool zeroAllowed = true);
    static BigNumberFloat random(BigNumberFloat max, bool zeroAllowed = true);

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        std::string num = toStdString();
        msgpack_pk.pack_str(num.size());
        msgpack_pk.pack_str_body(num.data(), num.size());
    }

    void msgpack_unpack(msgpack::object const &msgpack_o) {
        std::string num = msgpack_o.as<std::string>();
        *this = BigNumberFloat(num);
    }
};

inline bool operator<(const BigNumberFloat &l, const BigNumberFloat &r) {
    return l.data() < r.data();
}

inline bool operator>(const BigNumberFloat &l, const BigNumberFloat &r) {
    return l.data() > r.data();
}

inline bool operator<=(const BigNumberFloat &l, const BigNumberFloat &r) {
    return l.data() <= r.data();
}

inline bool operator>=(const BigNumberFloat &l, const BigNumberFloat &r) {
    return l.data() >= r.data();
}

inline bool operator==(const BigNumberFloat &l, const BigNumberFloat &r) {
    return l.data() == r.data();
}

inline bool operator!=(const BigNumberFloat &l, const BigNumberFloat &r) {
    return l.data() != r.data();
}

inline bool operator<(const BigNumberFloat &l, const int &r) {
    return l.data() < r;
}

inline bool operator>(const BigNumberFloat &l, const int &r) {
    return l.data() > r;
}

inline bool operator<=(const BigNumberFloat &l, const int &r) {
    return l.data() <= r;
}

inline bool operator>=(const BigNumberFloat &l, const int &r) {
    return l.data() >= r;
}

inline bool operator==(const BigNumberFloat &l, const int &r) {
    return l.data() == r;
}

inline bool operator!=(const BigNumberFloat &l, const int &r) {
    return l.data() != r;
}

inline size_t qHash(const BigNumberFloat &key, size_t seed) {
    return qHash(key, seed);
}

QDebug operator<<(QDebug debug, const BigNumberFloat &BigNumberFloat);
QDebug operator<<(QDebug debug, const boost::multiprecision::cpp_bin_float_50 &BigNumberFloat);
std::ostream &operator<<(std::ostream &os, const BigNumberFloat &BigNumberFloat);

#endif // BIGNUMBER_FLOAT_H
