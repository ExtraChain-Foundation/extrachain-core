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

#ifndef SIGN_INTERFACE_H
#define SIGN_INTERFACE_H

#include <QString>

class IApprover
{
public:
    virtual ~IApprover() = 0;
public:
    /**
     * @brief creates digital signature from data
     * @param data - serialized object
     * @return digitalSignature encoded with Base64
     */
    virtual QByteArray sign(const QByteArray &data) = 0;
    /**
     * @brief verifies that digital signature and data is correct
     * @param data - serialized object
     * @param dsign - digitalSignature encoded with Base64
     * @return true, if digitalSignature is right
     */
    virtual bool verify(const QByteArray &data, const QByteArray &dsignBase64) = 0;
    /**
     * @brief setter for public key
     * @param publicKey
     */
    virtual bool loadPublicKey(const QByteArray &key) = 0;
    /**
     * @brief Serialize method
     * @return public key string
     */
    virtual QByteArray extractPublicKey() = 0;
    /**
     * @brief gets key readable
     * @return public key string
     */
    virtual QByteArray getPublicKey() = 0;
};

inline IApprover::~IApprover() {}

#endif // SIGN_INTERFACE_H
