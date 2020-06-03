#ifndef ELLIPTICPOINT_H
#define ELLIPTICPOINT_H

#include "utils/utils.h"
#include "utils/bignumber.h"

class EllipticPoint
{
private:
    BigNumber xp;
    BigNumber yp;

public:
    EllipticPoint();
    EllipticPoint(BigNumber x, BigNumber y);
    ~EllipticPoint();

public:
    BigNumber x() const;
    void setX(const BigNumber &value);
    BigNumber y() const;
    void setY(const BigNumber &value);
    bool isZero();
};

#endif // ELLIPTICPOINT_H
