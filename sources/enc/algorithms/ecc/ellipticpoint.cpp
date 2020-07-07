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

#include "enc/algorithms/ecc/ellipticpoint.h"

EllipticPoint::EllipticPoint()
{
    xp = BigNumber(0);
    yp = BigNumber(0);
}

EllipticPoint::EllipticPoint(BigNumber x, BigNumber y)
{
    this->xp = x;
    this->yp = y;
}

EllipticPoint::~EllipticPoint()
{
}

BigNumber EllipticPoint::x() const
{
    return xp;
}

void EllipticPoint::setX(const BigNumber &value)
{
    xp = value;
    // std::cout << "x: " << x.toByteArray().toStdString() << std::endl;
}

BigNumber EllipticPoint::y() const
{
    return yp;
}

void EllipticPoint::setY(const BigNumber &value)
{
    yp = value;
    // std::cout << "y: " << y.toByteArray().toStdString() << std::endl;
}

bool EllipticPoint::isZero()
{
    if (xp == BigNumber(0) && yp == BigNumber(0))
        return true;
    else
        return false;
}
