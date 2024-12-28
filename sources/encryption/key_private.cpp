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

#include "encryption/key_private.h"
#include "encryption/encryption_tools.h"
#include "utils/exc_utils.h"

#include <sodium.h>

KeyPrivate::KeyPrivate(const PrivateKey &secret_key, const PublicKey &public_key) {
    secret_key_ = secret_key;
    public_key_ = public_key;
}

KeyPrivate::KeyPrivate(const std::string &secret_key, const std::string &public_key) {
    secret_key_ = ByteArray(secret_key).toArray<crypto_sign_SECRETKEYBYTES>();
    public_key_ = ByteArray(public_key).toArray<crypto_sign_PUBLICKEYBYTES>();
}

KeyPrivate::KeyPrivate(const KeyPrivate &keyPrivate) {
    secret_key_ = keyPrivate.secret_key();
    public_key_ = keyPrivate.public_key();
}

void KeyPrivate::generate() {
    auto [secret_key, public_key] = Cryptography::asymmetric_create_pair();
    secret_key_                   = secret_key;
    public_key_                   = public_key;
}

Cryptography::CryptoResult KeyPrivate::encrypt(const Bytes &data, const PublicKey &receiver_public_key) const {
    return Cryptography::asymmetric_encrypt(data, secret_key_, receiver_public_key);
}

Cryptography::CryptoResult KeyPrivate::decrypt(const Bytes &data, const PublicKey &sender_public_key) const {
    return Cryptography::asymmetric_decrypt(data, secret_key_, sender_public_key);
}

std::expected<bool, FsError> KeyPrivate::encrypt_file(const FsPath    &file,
                                                      const FsPath    &result_file,
                                                      const PublicKey &receiver_public_key) const {
    return Cryptography::asymmetric_encrypt_file(file, result_file, secret_key_, receiver_public_key);
}

std::expected<bool, FsError> KeyPrivate::decrypt_file(const FsPath    &file,
                                                      const FsPath    &result_file,
                                                      const PublicKey &sender_public_key) const {
    return Cryptography::asymmetric_decrypt_file(file, result_file, secret_key_, sender_public_key);
}

Cryptography::CryptoResult KeyPrivate::encrypt_self(const Bytes &data) const {
    return Cryptography::asymmetric_encrypt_self(data, secret_key_, public_key_);
}

Cryptography::CryptoResult KeyPrivate::decrypt_self(const Bytes &data) const {
    return Cryptography::asymmetric_decrypt_self(data, secret_key_, public_key_);
}

std::expected<bool, FsError> KeyPrivate::encrypt_self_file(const FsPath &input, const FsPath &result_file) const {
    return Cryptography::asymmetric_encrypt_self_file(input, result_file, secret_key_, public_key_);
}

std::expected<bool, FsError> KeyPrivate::decrypt_self_file(const FsPath &input, const FsPath &result_file) const {
    return Cryptography::asymmetric_decrypt_self_file(input, result_file, secret_key_, public_key_);
}

std::expected<Signature, Cryptography::CryptoError> KeyPrivate::sign(const Bytes &data) const {
    return Cryptography::sign(data, secret_key_);
}

std::expected<bool, Cryptography::CryptoError> KeyPrivate::verify(const Bytes     &data,
                                                                  const Signature &signature) const {
    return Cryptography::verify(data, public_key_, signature);
}

std::expected<Signature, Cryptography::CryptoError> KeyPrivate::sign(const std::string &data) const {
    return Cryptography::sign(ByteArray(data).toBytes(), secret_key_);
}

std::expected<bool, Cryptography::CryptoError> KeyPrivate::verify(const std::string &data,
                                                                  const Signature   &signature) const {
    return Cryptography::verify(ByteArray(data).toBytes(), public_key_, signature);
}

const PrivateKey &KeyPrivate::secret_key() const {
    return secret_key_;
}

const PublicKey &KeyPrivate::public_key() const {
    return public_key_;
}

bool KeyPrivate::empty() const {
    return Utils::is_container_empty(secret_key_) || Utils::is_container_empty(public_key_);
}
