#ifndef ELLIPTICPOINTS_H
#define ELLIPTICPOINTS_H

#include "utils/bignumber.h"

// y^2 = x^3 + ax + b mod p
// a=-5
// b=3
// p=
class EllipticPoints
{
private:
    BigNumber x;
    BigNumber y;

public:
    EllipticPoints(BigNumber x);
    static bool checkOnCurve(BigNumber x, BigNumber y);
    static EllipticPoints calcPointOnCurve(BigNumber x);

    EllipticPoints();
    EllipticPoints(BigNumber, BigNumber);
    EllipticPoints(const EllipticPoints &);
    inline BigNumber getX()
    {
        return this->x;
    }
    inline BigNumber getY()
    {
        return this->y;
    }
    inline void setX(BigNumber xCoord)
    {
        this->x = xCoord;
    }
    inline void setY(BigNumber yCoord)
    {
        this->y = yCoord;
    }
    // bool checkOnCurve(BigNumber x, BigNumber y);

    EllipticPoints operator*(const BigNumber &bigNumber);
    BigNumber operator*(const EllipticPoints &point);
    EllipticPoints operator+(EllipticPoints &point);
    QByteArray CryptMessage(QByteArray message);
};
namespace Curves
{
    static BigNumber aCurve(BigNumber("-5"));
    static BigNumber bCurve(BigNumber("3"));
    static BigNumber pCurve(BigNumber("74207281"));
}
#endif // ELLIPTICPOINTS_H
