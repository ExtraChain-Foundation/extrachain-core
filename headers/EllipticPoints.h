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
namespace Curves {
static BigNumber pX("55066263022277343669578718895168534326250603453777594175500187360389116729240");
static BigNumber pY("32670510020758816978083085130507043184471273380659243275938904335757337482424");
static BigNumber aCurve(BigNumber("-5"));
static BigNumber bCurve(BigNumber("3"));
static BigNumber pCurve(BigNumber("12287d72ae0b022f04f59075d446a6bb"));
}
#endif // ELLIPTICPOINTS_H
