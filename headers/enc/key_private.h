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

#ifndef KEY_PRIVATE_H
#define KEY_PRIVATE_H

#include <QDebug>

#include <msgpack.hpp>

#include "extrachain_global.h"

class EXTRACHAIN_EXPORT KeyPrivate {
private:
    std::string m_secretKey;
    std::string m_publicKey;

public:
    /**
     * @brief New keys
     */
    KeyPrivate() = default;
    /**
     * @brief Existing keys
     * @param keyPair - [prKey:pubKey]
     */
    KeyPrivate(const std::string &secret_key, const std::string &public_key);
    KeyPrivate(const KeyPrivate &keyPrivate);
    ~KeyPrivate() = default;

public:
    void generate();

    QByteArray encrypt(const QByteArray &data, const std::string &receiverPublicKey,
                       const std::string &nonce = "") const;
    QByteArray decrypt(const QByteArray &data, const std::string &senderPublicKey,
                       const std::string &nonce = "") const;
    QByteArray encryptSelf(const QByteArray &data) const;
    QByteArray decryptSelf(const QByteArray &data) const;

    QByteArray sign(const QByteArray &data) const;
    bool verify(const QByteArray &data, const QByteArray &dsignHex) const;

    const std::string &secretKey() const;
    const std::string &publicKey() const;

    bool empty() const;

    MSGPACK_DEFINE(m_secretKey, m_publicKey)
};

#endif // KEY_PRIVATE_H
