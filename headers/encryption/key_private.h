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
#include "utils/exc_magic.h"

#include <filesystem>

#include "encryption/encryption_tools.h"

class EXTRACHAIN_EXPORT KeyPrivate {
private:
    PrivateKey m_secretKey = PrivateKey();
    PublicKey  m_publicKey = PublicKey();

public:
    /**
     * @brief New keys
     */
    KeyPrivate() = default;
    /**
     * @brief Existing keys
     * @param keyPair - [prKey:pubKey]
     */
    explicit KeyPrivate(const PrivateKey &secret_key, const PublicKey &public_key);
    explicit KeyPrivate(const std::string &secret_key, const std::string &public_key);
    KeyPrivate(const KeyPrivate &keyPrivate);
    ~KeyPrivate() = default;

public:
    void generate();

    Bytes encrypt(const Bytes &data, const PublicKey &receiverPublicKey, const Nonce &nonce = Nonce()) const;
    Bytes decrypt(const Bytes &data, const PublicKey &senderPublicKey, const Nonce &nonce = Nonce()) const;
    Bytes encryptSelf(const Bytes &data) const;
    Bytes decryptSelf(const Bytes &data) const;

    void encryptFile(const std::filesystem::path &file, const std::filesystem::path &resultFile) const;
    void decryptFile(const std::filesystem::path &file, const std::filesystem::path &resultFile) const;

    Signature sign(const Bytes &data) const;
    bool      verify(const Bytes &data, const Signature &signature) const;

    // deprecated
    Signature sign(const std::string &data) const;
    bool      verify(const std::string &data, const Signature &signature) const;

    const PrivateKey &secretKey() const;
    const PublicKey  &publicKey() const;

    bool empty() const;

    MSGPACK_DEFINE(m_secretKey, m_publicKey)
    BOOST_DESCRIBE_CLASS(KeyPrivate, (), (), (), (m_secretKey, m_publicKey))
};

MAKE_MAGICAL(KeyPrivate)

#endif // KEY_PRIVATE_H
