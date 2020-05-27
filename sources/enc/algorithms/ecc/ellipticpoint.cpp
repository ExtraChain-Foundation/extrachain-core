#include "enc/algorithms/ecc/ellipticpoint.h"

EllipticPoint::EllipticPoint()
{
    xp = BigNumber(0);
    yp = BigNumber(0);
}

EllipticPoint::EllipticPoint(BigNumber x, BigNumber y)
{
    this->xp = x;
    this->yp = y;
}

EllipticPoint::~EllipticPoint()
{
}

BigNumber EllipticPoint::x() const
{
    return xp;
}

void EllipticPoint::setX(const BigNumber &value)
{
    xp = value;
    // std::cout << "x: " << x.toByteArray().toStdString() << std::endl;
}

BigNumber EllipticPoint::y() const
{
    return yp;
}

void EllipticPoint::setY(const BigNumber &value)
{
    yp = value;
    // std::cout << "y: " << y.toByteArray().toStdString() << std::endl;
}

bool EllipticPoint::isZero()
{
    if (xp == BigNumber(0) && yp == BigNumber(0))
        return true;
    else
        return false;
}
