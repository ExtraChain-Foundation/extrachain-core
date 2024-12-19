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

#include <string>

#include <utils/exc_utils.h>
#include <sodium.h>

using Bytes         = std::vector<std::uint8_t>;
using PrivateKey    = std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES>;
using PublicKey     = std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES>;
using Signature     = std::array<std::uint8_t, crypto_sign_BYTES>;
using Nonce         = std::array<std::uint8_t, crypto_box_NONCEBYTES>;
using Salt          = std::array<std::uint8_t, crypto_pwhash_SALTBYTES>;
using KeyBytes      = std::array<std::uint8_t, crypto_secretbox_KEYBYTES>;
using KeyPass       = std::array<std::uint8_t, crypto_box_SEEDBYTES>;
using Curve25519Key = std::array<std::uint8_t, crypto_scalarmult_curve25519_BYTES>;

namespace Cryptography {
    EXTRACHAIN_EXPORT KeyBytes keygen();

    EXTRACHAIN_EXPORT KeyPass key_from_password(const std::string &password, const Salt &salt = Salt());

    EXTRACHAIN_EXPORT Signature sign(const Bytes &data, const PrivateKey &secret_key);
    EXTRACHAIN_EXPORT bool      verify(const Bytes &data, const PublicKey &public_key, const Signature &signature);

    EXTRACHAIN_EXPORT Bytes symmetric_encrypt(const Bytes &data, const KeyPass &secret_key);
    EXTRACHAIN_EXPORT Bytes symmetric_decrypt(const Bytes &data, const KeyPass &secret_key);

    EXTRACHAIN_EXPORT std::string symmetric_encrypt(const std::string &data, const KeyPass &secret_key);
    EXTRACHAIN_EXPORT std::string symmetric_decrypt(const std::string &data, const KeyPass &secret_key);

    EXTRACHAIN_EXPORT Bytes symmetric_encrypt_password(const Bytes &data, const std::string &password);
    EXTRACHAIN_EXPORT Bytes symmetric_decrypt_password(const Bytes &data, const std::string &password);

    EXTRACHAIN_EXPORT std::pair<PrivateKey, PublicKey> asymmetric_create_pair();

    EXTRACHAIN_EXPORT Bytes asymmetric_encrypt(const Bytes      &data,
                                               const PrivateKey &sender_secret_key,
                                               const PublicKey  &receiver_public_key,
                                               const Nonce      &nonce = Nonce());
    EXTRACHAIN_EXPORT Bytes asymmetric_decrypt(const Bytes      &data,
                                               const PrivateKey &receiver_secret_key,
                                               const PublicKey  &sender_public_key,
                                               const Nonce      &nonce = Nonce());

    Bytes asymmetric_encrypt_self(const Bytes      &data,
                                  const PrivateKey &self_secret_key,
                                  const PublicKey  &self_public_key);
    Bytes asymmetric_decrypt_self(const Bytes      &data,
                                  const PrivateKey &self_secret_key,
                                  const PublicKey  &self_public_key);

    EXTRACHAIN_EXPORT std::expected<bool, FsError> symmetric_encrypt_file(const FsPath   &original_path,
                                                                          const FsPath   &encrypt_path,
                                                                          const KeyBytes &key,
                                                                          size_t          block_size = 60000);
    EXTRACHAIN_EXPORT std::expected<bool, FsError> symmetric_decrypt_file(const FsPath   &encrypt_path,
                                                                          const FsPath   &decrypt_path,
                                                                          const KeyBytes &key,
                                                                          size_t          block_size = 60000);

    EXTRACHAIN_EXPORT std::expected<bool, FsError> asymmetric_encrypt_file(const FsPath     &input_path,
                                                                           const FsPath     &output_path,
                                                                           const PrivateKey &receiver_secret_key,
                                                                           const PublicKey  &sender_public_key,
                                                                           const Nonce      &nonce      = Nonce(),
                                                                           size_t            block_size = 60000);
    EXTRACHAIN_EXPORT std::expected<bool, FsError> asymmetric_decrypt_file(const FsPath     &input_path,
                                                                           const FsPath     &output_path,
                                                                           const PrivateKey &receiver_secret_key,
                                                                           const PublicKey  &sender_public_key,
                                                                           const Nonce      &nonce      = Nonce(),
                                                                           size_t            block_size = 60000);

    EXTRACHAIN_EXPORT std::expected<bool, FsError> asymmetric_encrypt_self_file(
        const FsPath     &input_path,
        const FsPath     &output_path,
        const PrivateKey &self_secret_key,
        const PublicKey  &self_public_key,
        size_t            block_size = 60000);
    EXTRACHAIN_EXPORT std::expected<bool, FsError> asymmetric_decrypt_self_file(
        const FsPath     &input_path,
        const FsPath     &output_path,
        const PrivateKey &self_secret_key,
        const PublicKey  &self_public_key,
        size_t            block_size = 60000);
} // namespace Cryptography
