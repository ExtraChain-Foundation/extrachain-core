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

#ifndef ECCMATH_H
#define ECCMATH_H

#include "utils/bignumber.h"
#include "ellipticpoint.h"
#include "curves.h"
#include <QDebug>
#include <cassert>
#include <iostream>
namespace ECC {
/**
 * @brief Modular arithmetic
 * @param curve
 * @param k
 * @param p
 * @return
 */
BigNumber inverseMod(BigNumber k, BigNumber p);
/**
 * @brief isOnCurve
 * @param curve
 * @param point
 * @return
 */
bool isOnCurve(ECC::curve curve, EllipticPoint point);
/**
 * @brief negatePoint
 * @param curve
 * @param point
 * @return
 */
EllipticPoint negatePoint(ECC::curve curve, EllipticPoint point);
/**
 * @brief add points on curve
 * @param curve
 * @param a
 * @param b
 * @return
 */
EllipticPoint add(ECC::curve curve, EllipticPoint a, EllipticPoint b);
/**
 * @brief multiply
 * @param curve
 * @param k
 * @param p
 * @return
 */
EllipticPoint multiply(ECC::curve curve, BigNumber k, EllipticPoint point);
}

#endif // MATH_H
