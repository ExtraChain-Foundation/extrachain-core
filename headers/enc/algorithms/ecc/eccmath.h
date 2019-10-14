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
