#include "headers/crypt/ecc/ellipticpoint.h"

EllipticPoint::EllipticPoint()
{
    x = BigNumber("0");
    y = BigNumber("0");
}

EllipticPoint::EllipticPoint(BigNumber x, BigNumber y)
{
    this->x = x;
    this->y = y;
}

EllipticPoint::~EllipticPoint()
{
    //
}

QByteArray EllipticPoint::serialize()
{
    return x.toByteArray() + y.toByteArray();
}

BigNumber EllipticPoint::X() const
{
    return x;
}

void EllipticPoint::setX(const BigNumber &value)
{
    x = value;
}

BigNumber EllipticPoint::Y() const
{
    return y;
}

void EllipticPoint::setY(const BigNumber &value)
{
    y = value;
}

bool EllipticPoint::isZero()
{
    if (x == BigNumber("0") && y == BigNumber("0"))
        return true;
    else
        return false;
}
