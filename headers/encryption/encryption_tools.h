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

#pragma once

#include <string>

#include <utils/exc_utils.h>
#include <sodium.h>

using Bytes         = std::vector<uint8_t>;
using PrivateKey    = std::array<uint8_t, crypto_sign_SECRETKEYBYTES>;
using PublicKey     = std::array<uint8_t, crypto_sign_PUBLICKEYBYTES>;
using Signature     = std::array<uint8_t, crypto_sign_BYTES>;
using Nonce         = std::array<uint8_t, crypto_box_NONCEBYTES>;
using Salt          = std::array<uint8_t, crypto_pwhash_SALTBYTES>;
using KeyBytes      = std::array<uint8_t, crypto_secretbox_KEYBYTES>;
using KeyPass       = std::array<uint8_t, crypto_box_SEEDBYTES>;
using Curve25519Key = std::array<uint8_t, crypto_scalarmult_curve25519_BYTES>;

namespace Cryptography {
EXTRACHAIN_EXPORT KeyBytes keygen();

EXTRACHAIN_EXPORT KeyPass getKeyPassFromPassword(const std::string &pass, const Salt &salt = Salt());

EXTRACHAIN_EXPORT Signature sign(const Bytes &data, const PrivateKey &secret_key);
EXTRACHAIN_EXPORT bool verify(const Bytes &data, const PublicKey &public_key, const Signature &signature);

EXTRACHAIN_EXPORT Bytes encrypt(const Bytes &data, const KeyPass &secret_key);
EXTRACHAIN_EXPORT Bytes decrypt(const Bytes &data, const KeyPass &secret_key);

EXTRACHAIN_EXPORT std::string encrypt(const std::string &data, const KeyPass &secret_key);
EXTRACHAIN_EXPORT std::string decrypt(const std::string &data, const KeyPass &secret_key);

EXTRACHAIN_EXPORT Bytes encryptWithPassword(const Bytes &data, const std::string &password);
EXTRACHAIN_EXPORT Bytes decryptWithPassword(const Bytes &data, const std::string &password);

EXTRACHAIN_EXPORT std::pair<PrivateKey, PublicKey> createAsymmetricPair();
EXTRACHAIN_EXPORT Bytes                            encryptAsymmetric(
                               const Bytes      &data,
                               const PrivateKey &secret_key,
                               const PublicKey  &public_key,
                               const Nonce      &nonce = Nonce());
EXTRACHAIN_EXPORT Bytes decryptAsymmetric(
    const Bytes      &data,
    const PrivateKey &secret_key,
    const PublicKey  &public_key,
    const Nonce      &nonce = Nonce());
}
