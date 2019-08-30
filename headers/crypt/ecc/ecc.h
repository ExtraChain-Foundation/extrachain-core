#ifndef ECC_H
#define ECC_H

#include <QByteArray>

#include "utils/bignumber.h"
#include "EllipticPoints.h"

namespace ECC {
class Curve
{
public:
    Curve();
    ~Curve();
    void G();
};

namespace Primitives {
    // static BigNumber add(BigNumber a, BigNumber b);
    // static QByteArray sub(QByteArray a, QByteArray b);
    // static QByteArray mult(QByteArray a, QByteArray b);
    // static QByteArray div(QByteArray a, QByteArray b);
    // static QByteArray exp(QByteArray a, QByteArray b);
    // static QByteArray sqr(QByteArray a, QByteArray b);

    // EllipticPoints GPoint(BigNumber("2543532354353443"),BigNumber("3453432534534543"));
} // namespace Primitives
static EllipticPoints GPoint(BigNumber("2543532354353443"), BigNumber("3453432534534543"));
static BigNumber aCurve(BigNumber("-5"));
static BigNumber bCurve(BigNumber("20"));
BigNumber eea(BigNumber i, BigNumber j);
} // Elliptic Curve Cryptography namespace

#endif // ECC_H
