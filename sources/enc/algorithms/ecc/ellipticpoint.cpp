#include "enc/algorithms/ecc/ellipticpoint.h"

EllipticPoint::EllipticPoint()
{
    x = BigNumber(0);
    y = BigNumber(0);
}

EllipticPoint::EllipticPoint(const QByteArray &serialized)
{
    QList<QByteArray> list = Serialization::universalDeserialize(serialized, 2);
    x = BigNumber(list[0]);
    y = BigNumber(list[1]);
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
    QByteArray xb = x.toByteArray();
    QByteArray yb = y.toByteArray();
    QList<QByteArray> list = { xb, yb };
    return Serialization::universalSerialize(list, 2);
}

BigNumber EllipticPoint::X() const
{
    return x;
}

void EllipticPoint::setX(const BigNumber &value)
{
    x = value;
    //    std::cout << "x: " << x.toByteArray().toStdString() << std::endl;
}

BigNumber EllipticPoint::Y() const
{
    return y;
}

void EllipticPoint::setY(const BigNumber &value)
{
    y = value;
    //    std::cout << "y: " << y.toByteArray().toStdString() << std::endl;
}

bool EllipticPoint::isZero()
{
    if (x == BigNumber(0) && y == BigNumber(0))
        return true;
    else
        return false;
}
