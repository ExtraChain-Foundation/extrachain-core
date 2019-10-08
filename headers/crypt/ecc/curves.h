#ifndef CURVES_H
#define CURVES_H
#include "crypt/ecc/ellipticpoint.h"
#include "utils/bignumber.h"

namespace ECC {
class curve
{
public:
    BigNumber p = BigNumber("0");
    BigNumber a = BigNumber("0");
    BigNumber b = BigNumber("0");
    EllipticPoint g = EllipticPoint(BigNumber("0"), BigNumber("0"));
    BigNumber n = BigNumber("0");
    BigNumber h = BigNumber("0");
};
class secp256k1 : public curve
{
public:
    secp256k1()
    {
        this->p = BigNumber("fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");
        this->a = BigNumber("0");
        this->b = BigNumber("7");
        this->g =
            EllipticPoint(BigNumber("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"),
                          BigNumber("483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"));
        this->n = BigNumber("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
        this->h = BigNumber("1");
    }

    //    BigNumber eea(BigNumber a, BigNumber b)
    //    {
    //        BigNumber b0 = b, t, q;
    //        BigNumber x0 = 0, x1 = 1;

    //        if (b == 1)
    //            return 1;

    //        while (a > 1)
    //        {
    //            q = a / b;
    //            t = b, b = a % b, a = t;
    //            t = x0, x0 = x1 - q * x0, x1 = t;
    //        }

    //        if (x1 < 0)
    //            x1 = x1 + b0;
    //        return x1;
    //    }
};
}
#endif // CURVES_H
