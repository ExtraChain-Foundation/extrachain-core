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

#include <QtCore/QString>
#include <QtCore/QChar>
#include <QMetaType>
#include <string>
#include <sstream>
#include <QString>
#include <QRandomGenerator>
#include <QDebug>

#include "gmpxx.h"

#ifdef QT_DEBUG
#define UPDATE_DEBUG()                  \
    qdata = m_data.get_str(16).c_str(); \
    qdataDec = m_data.get_str(10).c_str();
#else
#define UPDATE_DEBUG()
#endif

/**
 * Data type for big hex numbers for addresses
 * example: ab11405c92a05c91c48
 */
namespace BigNumberUtils {
const static QVector<char> Chars = { 'a', 'b', 'c', 'd', 'e', 'f', '0', '1',
                                     '2', '3', '4', '5', '6', '7', '8', '9' };
}
class BigNumber
{
public:
    BigNumber();
    BigNumber(const QByteArray &bigNumber, int base = 16);
    BigNumber(const BigNumber &other);
    BigNumber(int number);
    BigNumber(long long number);
    BigNumber(mpz_class number);
    ~BigNumber() = default;

private:
    mpz_class m_data;
    bool infinity = false;

#ifdef QT_DEBUG
    QByteArray qdata;
    QByteArray qdataDec;
#endif

public:
    BigNumber operator&(const BigNumber &);
    BigNumber operator>>(const uint &);
    BigNumber operator>>=(const uint &);
    BigNumber operator+(const BigNumber &);
    BigNumber operator+(long long);
    BigNumber operator-(const BigNumber &);
    BigNumber operator-(long long);
    BigNumber operator*(const BigNumber &) const;
    BigNumber operator*(long long);
    BigNumber operator/(const BigNumber &);
    BigNumber operator/(long long);
    BigNumber operator%(const BigNumber &);
    BigNumber operator%(long long);
    BigNumber &operator=(const BigNumber &);
    BigNumber &operator=(long long);
    BigNumber &operator++();   // pre increment
    BigNumber operator++(int); // post increment
    BigNumber &operator--();   // pre increment
    BigNumber operator--(int); // post increment
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
    BigNumber operator-() const;

public:
    mpz_class data() const;
    int isProbPrime() const;
    bool isEmpty() const;
    QByteArray toByteArray(int base = 16) const;
    std::string toStdString(int base = 16) const;
    QByteArray toZeroByteArray(int size) const;
    QByteArray toActorId() const;
    BigNumber pow(unsigned long number);
    BigNumber sqrt(unsigned long number = 2) const;
    BigNumber abs() const;
    bool getInfinity() const;
    void setInfinity(bool value);
    BigNumber nextPrime();

    static bool isValid(const QByteArray &bigNumber, int base = 16);
    static BigNumber factorial(unsigned long number);
    static char binaryCompareAnd(char, char);
    static BigNumber random(int n, bool zeroAllowed = true);
    static BigNumber random(int n, const BigNumber &max, bool zeroAllowed = true);
    static BigNumber random(BigNumber max, bool zeroAllowed = true);
};

inline bool operator<(const BigNumber &l, const BigNumber &r)
{

    return l.data() < r.data();
}

inline bool operator>(const BigNumber &l, const BigNumber &r)
{
    return l.data() > r.data();
}

inline bool operator<=(const BigNumber &l, const BigNumber &r)
{
    return l.data() <= r.data();
}

inline bool operator>=(const BigNumber &l, const BigNumber &r)
{
    return l.data() >= r.data();
}

inline bool operator==(const BigNumber &l, const BigNumber &r)
{
    return l.data() == r.data();
}

inline bool operator!=(const BigNumber &l, const BigNumber &r)
{
    return l.data() != r.data();
}

inline bool operator<(const BigNumber &l, const int &r)
{

    return l.data() < r;
}

inline bool operator>(const BigNumber &l, const int &r)
{
    return l.data() > r;
}

inline bool operator<=(const BigNumber &l, const int &r)
{
    return l.data() <= r;
}

inline bool operator>=(const BigNumber &l, const int &r)
{
    return l.data() >= r;
}

inline bool operator==(const BigNumber &l, const int &r)
{
    return l.data() == r;
}

inline bool operator!=(const BigNumber &l, const int &r)
{
    return l.data() != r;
}

inline uint qHash(const BigNumber &key, uint seed)
{
    return qHash(key.toByteArray(), seed);
}

QDebug operator<<(QDebug debug, const BigNumber &bigNumber);
QDebug operator<<(QDebug debug, const mpz_class &bigNumber);

#endif // BIGNUMBER_H
