#ifndef CURVES_H
#define CURVES_H
#include "enc/algorithms/ecc/ellipticpoint.h"
#include "utils/bignumber.h"

namespace ECC {

namespace secp256k1 {
    const BigNumber p = BigNumber("fffffffffffffffffffffffffffffffffffffffffffffffffffffffefffffc2f");
    const BigNumber a = BigNumber("0");
    const BigNumber b = BigNumber("7");
    const EllipticPoint g =
        EllipticPoint(BigNumber("79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798"),
                      BigNumber("483ada7726a3c4655da4fbfc0e1108a8fd17b448a68554199c47d08ffb10d4b8"));
    const BigNumber n = BigNumber("fffffffffffffffffffffffffffffffebaaedce6af48a03bbfd25e8cd0364141");
    const BigNumber h = BigNumber("1");
}
struct curve
{
    const BigNumber p = secp256k1::p;
    const BigNumber a = secp256k1::a;
    const BigNumber b = secp256k1::b;
    const EllipticPoint g = secp256k1::g;
    const BigNumber n = secp256k1::n;
    const BigNumber h = secp256k1::h;
};
}
#endif // CURVES_H
