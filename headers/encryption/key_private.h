/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once

#include <boost/describe/class.hpp>
#include <msgpack.hpp>

#include "extrachain_global.h"
#include "core/byte_array.h"
#include "encryption/encryption_tools.h"

class EXTRACHAIN_EXPORT KeyPrivate {
private:
    PrivateKey secret_key_ = PrivateKey();
    PublicKey  public_key_ = PublicKey();

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
    void generate_random();
    void generate_seed(const MasterSeed &seed, int index);
    void generate_seed(const MasterSeed &seed, const std::string &label);

    Cryptography::CryptoResult encrypt(const Bytes &data, const PublicKey &receiver_public_key) const;
    Cryptography::CryptoResult decrypt(const Bytes &data, const PublicKey &sender_public_key) const;

    std::expected<bool, FsError> encrypt_file(const FsPath    &file,
                                              const FsPath    &result_file,
                                              const PublicKey &receiver_public_key) const;
    std::expected<bool, FsError> decrypt_file(const FsPath    &file,
                                              const FsPath    &result_file,
                                              const PublicKey &sender_public_key) const;

    Cryptography::CryptoResult encrypt_self(const Bytes &data) const;
    Cryptography::CryptoResult decrypt_self(const Bytes &data) const;

    std::expected<bool, FsError> encrypt_self_file(const FsPath &file, const FsPath &result_file) const;
    std::expected<bool, FsError> decrypt_self_file(const FsPath &file, const FsPath &result_file) const;

    std::expected<Signature, Cryptography::CryptoError> sign(const Bytes &data) const;
    std::expected<bool, Cryptography::CryptoError> verify(const Bytes &data, const Signature &signature) const;

    // [[deprecated("Use sign version with Bytes")]]
    std::expected<Signature, Cryptography::CryptoError> sign(const std::string &data) const;
    // [[deprecated("Use verify version with Bytes")]]
    std::expected<bool, Cryptography::CryptoError> verify(const std::string &data,
                                                          const Signature   &signature) const;

    const PrivateKey &secret_key() const;
    const PublicKey  &public_key() const;

    bool empty() const;

    MSGPACK_DEFINE(secret_key_, public_key_)
    BOOST_DESCRIBE_CLASS(KeyPrivate, (), (), (), (secret_key_, public_key_))
};
