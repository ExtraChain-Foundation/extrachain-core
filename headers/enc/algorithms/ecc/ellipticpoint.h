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
