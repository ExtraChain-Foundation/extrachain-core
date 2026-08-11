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

#include <expected>
#include <string>
#include <array>
#include <vector>
#include <expected>
#include "extrachain_global.h"
#include "utils/fs_path.h"
#include <sodium.h>

static_assert(crypto_box_NONCEBYTES == crypto_secretbox_NONCEBYTES,
              "Nonce sizes must match between crypto_box and crypto_secretbox");
static_assert(crypto_box_NONCEBYTES >= 24, "Nonce size must be at least 24 bytes for security");
static_assert(crypto_secretbox_KEYBYTES >= 32, "Key size must be at least 32 bytes");

using Bytes         = std::vector<std::uint8_t>;
using PrivateKey    = std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES>;
using PublicKey     = std::array<std::uint8_t, crypto_sign_PUBLICKEYBYTES>;
using MasterSeed    = std::array<std::uint8_t, crypto_sign_ed25519_SEEDBYTES>;
using Signature     = std::array<std::uint8_t, crypto_sign_BYTES>;
using Nonce         = std::array<std::uint8_t, crypto_box_NONCEBYTES>;
using Salt          = std::array<std::uint8_t, crypto_pwhash_SALTBYTES>;
using KeyBytes      = std::array<std::uint8_t, crypto_secretbox_KEYBYTES>;
using KeyPass       = std::array<std::uint8_t, crypto_box_SEEDBYTES>;
using Curve25519Key = std::array<std::uint8_t, crypto_scalarmult_curve25519_BYTES>;

namespace Cryptography {
    enum class CryptoError {
        EmptyData,
        EmptyKey,
        EmptySign,
        EncryptionFailed,
        DecryptionFailed,
        DataTooShort,
        DataTooLarge,
        KeyConversionFailed,
        AuthenticationFailed,
        InvalidPath,
        FileAccessError
    };

    enum class MnemonicError {
        Empty,
        Validate
    };

    using CryptoResult = std::expected<Bytes, CryptoError>;

    constexpr size_t MIN_ENCRYPTED_SIZE_SYMMETRIC  = crypto_secretbox_MACBYTES + crypto_secretbox_NONCEBYTES;
    constexpr size_t MIN_ENCRYPTED_SIZE_ASYMMETRIC = crypto_box_MACBYTES + crypto_box_NONCEBYTES;

    EXTRACHAIN_EXPORT KeyBytes keygen();
    EXTRACHAIN_EXPORT std::expected<KeyPass, CryptoError> key_from_password(const std::string &password,
                                                                            const Salt        &salt = Salt());

    EXTRACHAIN_EXPORT std::expected<Signature, CryptoError> sign(const Bytes &data, const PrivateKey &secret_key);
    EXTRACHAIN_EXPORT std::expected<bool, CryptoError> verify(const Bytes     &data,
                                                              const PublicKey &public_key,
                                                              const Signature &signature);

    EXTRACHAIN_EXPORT CryptoResult symmetric_encrypt(const Bytes   &data,
                                                     const KeyPass &secret_key,
                                                     bool           nonce_from_key = false);
    EXTRACHAIN_EXPORT CryptoResult symmetric_decrypt(const Bytes   &data,
                                                     const KeyPass &secret_key,
                                                     bool           nonce_from_key = false);

    EXTRACHAIN_EXPORT CryptoResult symmetric_encrypt_password(const Bytes       &data,
                                                              const std::string &password,
                                                              bool               nonce_from_key = false);
    EXTRACHAIN_EXPORT CryptoResult symmetric_decrypt_password(const Bytes       &data,
                                                              const std::string &password,
                                                              bool               nonce_from_key = false);

    EXTRACHAIN_EXPORT std::pair<PrivateKey, PublicKey> asymmetric_create_pair();

    EXTRACHAIN_EXPORT MasterSeed generate_seed();
    EXTRACHAIN_EXPORT std::pair<PrivateKey, PublicKey> asymmetric_from_seed(const MasterSeed &master_seed,
                                                                            std::uint32_t     index);
    EXTRACHAIN_EXPORT std::pair<PrivateKey, PublicKey> asymmetric_from_seed(const MasterSeed  &master_seed,
                                                                            const std::string &label);
    EXTRACHAIN_EXPORT PublicKey                        get_public_from_private(const PrivateKey &private_key);

    EXTRACHAIN_EXPORT std::vector<std::string> create_mnemonic(const MasterSeed &master_seed);
    EXTRACHAIN_EXPORT std::expected<MasterSeed, MnemonicError> restore_seed_from_mnemonic(
        const std::string &mnemonic);
    EXTRACHAIN_EXPORT bool validate_mnemonic(const std::string &mnemonic);

    EXTRACHAIN_EXPORT CryptoResult asymmetric_encrypt(const Bytes      &data,
                                                      const PrivateKey &sender_secret_key,
                                                      const PublicKey  &receiver_public_key);
    EXTRACHAIN_EXPORT CryptoResult asymmetric_decrypt(const Bytes      &data,
                                                      const PrivateKey &receiver_secret_key,
                                                      const PublicKey  &sender_public_key);

    EXTRACHAIN_EXPORT CryptoResult asymmetric_encrypt_self(const Bytes      &data,
                                                           const PrivateKey &self_secret_key,
                                                           const PublicKey  &self_public_key);
    EXTRACHAIN_EXPORT CryptoResult asymmetric_decrypt_self(const Bytes      &data,
                                                           const PrivateKey &self_secret_key,
                                                           const PublicKey  &self_public_key);

    EXTRACHAIN_EXPORT std::expected<bool, FsError> validate_encryption_paths(const FsPath &input_path,
                                                                             const FsPath &output_path);

    EXTRACHAIN_EXPORT std::expected<bool, FsError> symmetric_encrypt_file(const FsPath  &original_path,
                                                                          const FsPath  &encrypt_path,
                                                                          const KeyPass &key,
                                                                          size_t         block_size = 60000);
    EXTRACHAIN_EXPORT std::expected<bool, FsError> symmetric_decrypt_file(const FsPath  &encrypt_path,
                                                                          const FsPath  &decrypt_path,
                                                                          const KeyPass &key,
                                                                          size_t         block_size = 60000);

    EXTRACHAIN_EXPORT std::expected<bool, FsError> symmetric_encrypt_file_password(const FsPath &original_path,
                                                                                   const FsPath &encrypt_path,
                                                                                   const std::string &password,
                                                                                   size_t block_size = 60000);
    EXTRACHAIN_EXPORT std::expected<bool, FsError> symmetric_decrypt_file_password(const FsPath      &encrypt_path,
                                                                                   const FsPath      &decrypt_path,
                                                                                   const std::string &password,
                                                                                   size_t block_size = 60000);

    EXTRACHAIN_EXPORT std::expected<bool, FsError> asymmetric_encrypt_file(const FsPath     &input_path,
                                                                           const FsPath     &output_path,
                                                                           const PrivateKey &sender_secret_key,
                                                                           const PublicKey  &receiver_public_key,
                                                                           size_t            block_size = 60000);
    EXTRACHAIN_EXPORT std::expected<bool, FsError> asymmetric_decrypt_file(const FsPath     &input_path,
                                                                           const FsPath     &output_path,
                                                                           const PrivateKey &receiver_secret_key,
                                                                           const PublicKey  &sender_public_key,
                                                                           size_t            block_size = 60000);

    EXTRACHAIN_EXPORT std::expected<bool, FsError> asymmetric_encrypt_self_file(const FsPath     &input_path,
                                                                                const FsPath     &output_path,
                                                                                const PrivateKey &self_secret_key,
                                                                                const PublicKey  &self_public_key,
                                                                                size_t block_size = 60000);
    EXTRACHAIN_EXPORT std::expected<bool, FsError> asymmetric_decrypt_self_file(const FsPath     &input_path,
                                                                                const FsPath     &output_path,
                                                                                const PrivateKey &self_secret_key,
                                                                                const PublicKey  &self_public_key,
                                                                                size_t block_size = 60000);
} // namespace Cryptography
