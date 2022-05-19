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

#include <filesystem>

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

    std::string encrypt(const std::string &data, const std::string &receiverPublicKey,
                        const std::string &nonce = "") const;
    std::string decrypt(const std::string &data, const std::string &senderPublicKey,
                        const std::string &nonce = "") const;
    std::string encryptSelf(const std::string &data) const;
    std::string decryptSelf(const std::string &data) const;

    void encryptFile(const std::filesystem::path &file, const std::filesystem::path &resultFile) const;
    void decryptFile(const std::filesystem::path &file, const std::filesystem::path &resultFile) const;

    std::string sign(const std::string &data) const;
    bool verify(const std::string &data, const std::string &signature) const;

    const std::string &secretKey() const;
    const std::string &publicKey() const;

    bool empty() const;

    MSGPACK_DEFINE(m_secretKey, m_publicKey)
};

QDebug operator<<(QDebug debug, const KeyPrivate &key);
std::ostream &operator<<(std::ostream &os, const KeyPrivate &key);

#endif // KEY_PRIVATE_H
