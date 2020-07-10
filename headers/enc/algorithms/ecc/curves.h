/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

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
