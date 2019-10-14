#ifndef CURVES_H
#define CURVES_H
#include "enc/algorithms/ecc/ellipticpoint.h"
#include "utils/bignumber.h"

namespace ECC {

namespace secp256k1 {
    static BigNumber p = BigNumber("fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");
    static BigNumber a = BigNumber("0");
    static BigNumber b = BigNumber("7");
    static EllipticPoint g =
        EllipticPoint(BigNumber("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"),
                      BigNumber("483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"));
    static BigNumber n = BigNumber("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
    static BigNumber h = BigNumber("1");
}
struct curve
{

    BigNumber p = secp256k1::p;
    BigNumber a = secp256k1::a;
    BigNumber b = secp256k1::b;
    EllipticPoint g = secp256k1::g;
    BigNumber n = secp256k1::n;
    BigNumber h = secp256k1::h;
};
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
}
#endif // CURVES_H
