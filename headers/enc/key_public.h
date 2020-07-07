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

#ifndef KEY_PUBLIC_H
#define KEY_PUBLIC_H

//#include <string>
//#include <sstream>
//#include <iostream>
//#include <cstring>

//#include <QDir>
//#include <QDebug>
#include "enc/algorithms/blowfish_crypt.h"
#include "utils/bignumber.h"
#include "enc/algorithms/ecc/eccmath.h"
#include "enc/algorithms/ecc/curves.h"
#include "enc/algorithms/ecc/ellipticpoint.h"

class KeyPublic
{
private:
    ECC::curve curve;
    EllipticPoint pbkey;

public:
    /**
     * @brief Existing keys
     * @param keyPair - [prKey:pubKey]
     */
    KeyPublic(EllipticPoint pubKey);
    KeyPublic(const QJsonObject &json);
    KeyPublic(const KeyPublic &keyPublic);
    KeyPublic();
    ~KeyPublic();

public: // Cryptor interface
    QByteArray encrypt(const QByteArray &data);

public: // Signer interface
    bool verify(const QByteArray &data, const QByteArray &dsignBase64);

public:
    bool isEmpty();
    EllipticPoint getPublicKey() const;
};

#endif // KEY_PUBLIC_H
