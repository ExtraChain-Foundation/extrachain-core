#ifndef ELLIPTICPOINT_H
#define ELLIPTICPOINT_H

#include "utils/utils.h"
#include "utils/bignumber.h"

class EllipticPoint
{
private:
    BigNumber x;
    BigNumber y;

public:
    EllipticPoint();
    EllipticPoint(const QByteArray &serialized);
    EllipticPoint(BigNumber x, BigNumber y);
    ~EllipticPoint();

public:
    QByteArray serialize();

public:
    BigNumber X() const;
    void setX(const BigNumber &value);
    BigNumber Y() const;
    void setY(const BigNumber &value);
    bool isZero();
};

#endif // ELLIPTICPOINT_H
