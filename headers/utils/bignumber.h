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

#include "utils/utils.h"

using std::string;

/**
 * Data type for big hex numbers for addresses
 * example: ab11405c92a05c91c48
 */

class BigNumber
{
public:
    BigNumber();
    BigNumber(const QByteArray &bigNumber);
    BigNumber(const BigNumber &other);
    BigNumber(int number);
    BigNumber(long long number);
    ~BigNumber();

private:
    QString hexValue = "";
    bool positive = true;
    static const int HEX_BASE = 16;
    static const int DEC_BASE = 10;
    static const int LONG_LONG_LENGTH = 19;

private:
    QString toHex(const QString &dec) const;
    QString toDec(const QString &hex) const;
    QString fillZeros(const QString &number) const; // fill string with zeros up to 19 numbers
    QString cutZeros(const QString &number) const;
    static std::pair<BigNumber, BigNumber> naiveDivide(BigNumber &value, const BigNumber &divider);

public:
    static std::pair<BigNumber, BigNumber> divide(BigNumber val, BigNumber divider);
    static int compare(const QString &one, const QString &two);
    BigNumber operator&(const BigNumber &);
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
    BigNumber &operator-();

public:
    bool isPrime() const;
    bool isEmpty() const;
    bool isPositive() const;
    QString getHexValue() const;
    QByteArray toBinary() const;
    QString toString() const;
    QByteArray toByteArray() const; // todo: change to serialize
    QByteArray serialize() const;
    BigNumber abs() const;
    void setHexValue(const QString &hex);
    void setPositive(bool newPositive);
    BigNumber pow(unsigned long long number);
    QString toStringDec() const;
    void fromString(QString serialized);

    static BigNumber fromByteArray(QByteArray serialized);
    static BigNumber factorial(int num);
    static BigNumber sqrt(const BigNumber &);
    static char binaryCompareAnd(char, char);
    static BigNumber random(int n);
    static BigNumber random(int n, const BigNumber &max);
    static BigNumber fromDec(const QByteArray &dec);
};

inline bool operator<(const BigNumber &e1, const BigNumber &e2)
{
    if (!e1.isPositive() && e2.isPositive())
        return true;

    if (e1.isPositive() && !e2.isPositive())
        return false;

    if (!e1.isPositive() && !e2.isPositive())
        return BigNumber::compare(e1.getHexValue(), e2.getHexValue()) > 0;

    return BigNumber::compare(e1.getHexValue(), e2.getHexValue()) < 0;
}

inline bool operator<=(const BigNumber &lhs, const BigNumber &rhs)
{
    return !(rhs < lhs);
}

inline bool operator>(const BigNumber &lhs, const BigNumber &rhs)
{
    return rhs < lhs;
}

inline bool operator>=(const BigNumber &lhs, const BigNumber &rhs)
{
    return !(lhs < rhs);
}

inline bool operator==(const BigNumber &e1, const BigNumber &e2)
{
    return e1.getHexValue() == e2.getHexValue() && e1.isPositive() == e2.isPositive();
}

inline bool operator!=(const BigNumber &e1, const BigNumber &e2)
{
    return !(e1 == e2);
}

inline uint qHash(const BigNumber &key, uint seed)
{
    return qHash(key.getHexValue(), seed) ^ key.isPositive();
}

Q_DECLARE_METATYPE(BigNumber)
Q_DECLARE_METATYPE(BigNumber *)

QDataStream &operator<<(QDataStream &in, BigNumber &bigNumber);
QDataStream &operator>>(QDataStream &out, BigNumber &bigNumber);
QDebug operator<<(QDebug debug, const BigNumber &bigNumber);

#endif // BIGNUMBER_H
