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

#include <iostream>
//#include "utils/utils.h"

#include "gmpxx.h"

#ifdef QT_DEBUG
#define UPDATE_DEBUG() qdata = m_data.get_str(16).c_str();
#else
#define UPDATE_DEBUG()
#endif

/**
 * Data type for big hex numbers for addresses
 * example: ab11405c92a05c91c48
 */

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
#endif

public:
    BigNumber operator&(const BigNumber &);
    BigNumber operator>>(const uint &);
    BigNumber operator>>=(const uint &);
    BigNumber operator+(const BigNumber &);
    BigNumber operator+(long long);
    BigNumber operator-(const BigNumber &);
    BigNumber operator-(long long);
    BigNumber operator*(const BigNumber &);
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
    BigNumber operator-();

public:
    mpz_class data() const;
    int isProbPrime() const;
    bool isEmpty() const;
    QByteArray toByteArray(int base = 16) const; // todo: change to serialize
    QByteArray toActorId() const;
    QByteArray serialize() const;
    BigNumber pow(unsigned long number);
    BigNumber sqrt(unsigned long number = 2) const;
    bool getInfinity() const;
    void setInfinity(bool value);

    static BigNumber factorial(unsigned long number);
    static char binaryCompareAnd(char, char);
    static BigNumber random(int n);
    static BigNumber random(int n, const BigNumber &max);
    static BigNumber random(BigNumber max);
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
