#ifndef BIGNUMBERDEC_H
#define BIGNUMBERDEC_H

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

class BigNumberDec
{
public:
    BigNumberDec();
    BigNumberDec(const QByteArray &bigNumber);
    BigNumberDec(const BigNumberDec &other);
    BigNumberDec(int number);
    BigNumberDec(long long number);
    ~BigNumberDec();

private:
    QString hexValue = "";
    bool positive = true;
    static const int HEX_BASE = 10;
    static const int DEC_BASE = 10;
    static const int LONG_LONG_LENGTH = 19;

private:
    QString toHex(const QString &dec) const;
    QString toDec(const QString &hex) const;
    QString fillZeros(const QString &number) const; // fill string with zeros up to 19 numbers
    QString cutZeros(const QString &number) const;
    static std::pair<BigNumberDec, BigNumberDec> naiveDivide(BigNumberDec &value,
                                                             const BigNumberDec &divider);

public:
    static std::pair<BigNumberDec, BigNumberDec> divide(BigNumberDec val, BigNumberDec divider);
    static int compare(const QString &one, const QString &two);
    BigNumberDec operator&(const BigNumberDec &);
    BigNumberDec operator+(const BigNumberDec &);
    BigNumberDec operator+(long long);
    BigNumberDec operator-(const BigNumberDec &);
    BigNumberDec operator-(long long);
    BigNumberDec operator*(const BigNumberDec &);
    BigNumberDec operator*(long long);
    BigNumberDec operator/(const BigNumberDec &);
    BigNumberDec operator/(long long);
    BigNumberDec operator%(const BigNumberDec &);
    BigNumberDec operator%(long long);
    BigNumberDec &operator=(const BigNumberDec &);
    BigNumberDec &operator=(long long);
    BigNumberDec &operator++();   // pre increment
    BigNumberDec operator++(int); // post increment
    BigNumberDec &operator--();   // pre increment
    BigNumberDec operator--(int); // post increment
    BigNumberDec &operator+=(const BigNumberDec &);
    BigNumberDec &operator+=(long long);
    BigNumberDec &operator-=(const BigNumberDec &);
    BigNumberDec &operator-=(long long);
    BigNumberDec &operator*=(const BigNumberDec &);
    BigNumberDec &operator*=(long long);
    BigNumberDec &operator/=(const BigNumberDec &);
    BigNumberDec &operator/=(long long);
    BigNumberDec &operator%=(const BigNumberDec &);
    BigNumberDec &operator%=(long long);
    BigNumberDec &operator-();

public:
    bool isPrime() const;
    bool isEmpty() const;
    bool isPositive() const;
    QString getHexValue() const;
    QByteArray toBinary() const;
    QString toString() const;
    QByteArray toByteArray() const; // todo: change to serialize
    QByteArray serialize() const;
    BigNumberDec abs() const;
    void setHexValue(const QString &hex);
    void setPositive(bool newPositive);
    BigNumberDec pow(unsigned long long number);
    QString toStringDec() const;
    void fromString(QString serialized);

    static BigNumberDec fromByteArray(QByteArray serialized);
    static BigNumberDec factorial(int num);
    static BigNumberDec sqrt(const BigNumberDec &);
    static char binaryCompareAnd(char, char);
    static BigNumberDec random(int n);
    static BigNumberDec random(int n, const BigNumberDec &max);
};

inline bool operator<(const BigNumberDec &e1, const BigNumberDec &e2)
{
    if (!e1.isPositive() && e2.isPositive())
        return true;

    if (e1.isPositive() && !e2.isPositive())
        return false;

    if (!e1.isPositive() && !e2.isPositive())
        return BigNumberDec::compare(e1.getHexValue(), e2.getHexValue()) > 0;

    return BigNumberDec::compare(e1.getHexValue(), e2.getHexValue()) < 0;
}

inline bool operator<=(const BigNumberDec &lhs, const BigNumberDec &rhs)
{
    return !(rhs < lhs);
}

inline bool operator>(const BigNumberDec &lhs, const BigNumberDec &rhs)
{
    return rhs < lhs;
}

inline bool operator>=(const BigNumberDec &lhs, const BigNumberDec &rhs)
{
    return !(lhs < rhs);
}

inline bool operator==(const BigNumberDec &e1, const BigNumberDec &e2)
{
    return e1.getHexValue() == e2.getHexValue() && e1.isPositive() == e2.isPositive();
}

inline bool operator!=(const BigNumberDec &e1, const BigNumberDec &e2)
{
    return !(e1 == e2);
}

inline uint qHash(const BigNumberDec &key, uint seed)
{
    return qHash(key.getHexValue(), seed) ^ key.isPositive();
}

Q_DECLARE_METATYPE(BigNumberDec)
Q_DECLARE_METATYPE(BigNumberDec *)

QDataStream &operator<<(QDataStream &in, BigNumberDec &bigNumber);
QDataStream &operator>>(QDataStream &out, BigNumberDec &bigNumber);
QDebug operator<<(QDebug debug, const BigNumberDec &bigNumber);

#endif // BIGNUMBER_H
