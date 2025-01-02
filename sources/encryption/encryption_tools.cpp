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

#include "encryption/encryption_tools.h"

using Cryptography::CryptoError;

namespace {
    bool validate_file_basic(const FsPath& path) {
        auto exists = path.exists();
        if (!exists || !*exists)
            return false;
        auto is_file = path.is_regular_file();
        if (!is_file || !*is_file)
            return false;
        return true;
    }
} // namespace

KeyBytes Cryptography::keygen() {
    KeyBytes key;
    crypto_secretbox_keygen(key.data());
    return key;
}

std::expected<KeyPass, Cryptography::CryptoError> Cryptography::key_from_password(const std::string& password,
                                                                                  const Salt&        salt) {
    if (password.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }

    Salt vsalt;
    if (Utils::is_container_empty(salt)) {
        std::fill(vsalt.begin(), vsalt.end(), '0');
    } else {
        vsalt = salt;
    }

    KeyPass key;
    int     rst1 = crypto_pwhash(key.data(),
                             key.size(),
                             password.data(),
                             password.size(),
                             vsalt.data(),
                             crypto_pwhash_OPSLIMIT_INTERACTIVE,
                             crypto_pwhash_MEMLIMIT_INTERACTIVE,
                             crypto_pwhash_ALG_DEFAULT);
    if (rst1 != 0) {
        return std::unexpected(CryptoError::EncryptionFailed);
    }
    return key;
}

std::expected<Signature, Cryptography::CryptoError> Cryptography::sign(const Bytes&      data,
                                                                       const PrivateKey& secret_key) {
    if (data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }
    if (Utils::is_container_empty(secret_key)) {
        return std::unexpected(CryptoError::EmptyKey);
    }

    Signature sig;
    crypto_sign_detached(sig.data(), NULL, data.data(), data.size(), secret_key.data());
    return sig;
}

std::expected<bool, Cryptography::CryptoError> Cryptography::verify(const Bytes&     data,
                                                                    const PublicKey& public_key,
                                                                    const Signature& signature) {
    if (data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }
    if (Utils::is_container_empty(public_key)) {
        return std::unexpected(CryptoError::EmptyKey);
    }
    if (Utils::is_container_empty(signature)) {
        return std::unexpected(CryptoError::EmptySign);
    }

    int res = crypto_sign_verify_detached(signature.data(), data.data(), data.size(), public_key.data());
    return res == 0;
}

Cryptography::CryptoResult Cryptography::symmetric_encrypt(const Bytes& data, const KeyPass& secret_key) {
    if (data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }

    if (Utils::is_container_empty(secret_key)) {
        return std::unexpected(CryptoError::EmptyKey);
    }

    Bytes encrypted(crypto_secretbox_MACBYTES + data.size());
    Nonce nonce;
    randombytes_buf(nonce.data(), nonce.size());

    if (crypto_secretbox_easy(encrypted.data(), data.data(), data.size(), nonce.data(), secret_key.data()) != 0) {
        return std::unexpected(CryptoError::EncryptionFailed);
    }

    Bytes result(nonce.size() + encrypted.size());
    std::copy(nonce.begin(), nonce.end(), result.begin());
    std::copy(encrypted.begin(), encrypted.end(), result.begin() + nonce.size());
    return result;
}

Cryptography::CryptoResult Cryptography::symmetric_decrypt(const Bytes&   encrypted_data,
                                                           const KeyPass& secret_key) {
    if (encrypted_data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }

    if (Utils::is_container_empty(secret_key)) {
        return std::unexpected(CryptoError::EmptyKey);
    }

    if (encrypted_data.size() < MIN_ENCRYPTED_SIZE_SYMMETRIC) {
        return std::unexpected(CryptoError::DataTooShort);
    }

    Nonce nonce;
    std::copy_n(encrypted_data.begin(), crypto_secretbox_NONCEBYTES, nonce.begin());

    Bytes encrypted_message(encrypted_data.begin() + crypto_secretbox_NONCEBYTES, encrypted_data.end());
    Bytes decrypted_message(encrypted_message.size() - crypto_secretbox_MACBYTES);

    if (crypto_secretbox_open_easy(decrypted_message.data(),
                                   encrypted_message.data(),
                                   encrypted_message.size(),
                                   nonce.data(),
                                   secret_key.data())
        != 0) {
        return std::unexpected(CryptoError::DecryptionFailed);
    }

    return decrypted_message;
}

Cryptography::CryptoResult Cryptography::symmetric_encrypt_password(const Bytes&       data,
                                                                    const std::string& password) {
    if (data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }

    if (password.empty()) {
        return std::unexpected(CryptoError::EmptyKey);
    }

    auto key_result = key_from_password(password);
    if (!key_result.has_value()) {
        return std::unexpected(CryptoError::KeyConversionFailed);
    }

    return symmetric_encrypt(data, key_result.value());
}

Cryptography::CryptoResult Cryptography::symmetric_decrypt_password(const Bytes&       data,
                                                                    const std::string& password) {
    if (data.empty())
        return std::unexpected(CryptoError::EmptyData);

    if (password.empty()) {
        return std::unexpected(CryptoError::EmptyKey);
    }

    auto key_result = key_from_password(password);
    if (!key_result.has_value())
        return std::unexpected(CryptoError::KeyConversionFailed);

    return symmetric_decrypt(data, key_result.value());
}

std::pair<PrivateKey, PublicKey> Cryptography::asymmetric_create_pair() {
    PrivateKey sk;
    PublicKey  pk;
    crypto_sign_keypair(pk.data(), sk.data());
    return { sk, pk };
}

Cryptography::CryptoResult Cryptography::asymmetric_encrypt(const Bytes&      data,
                                                            const PrivateKey& sender_secret_key,
                                                            const PublicKey&  receiver_public_key) {
    if (data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }

    Curve25519Key x_secret_key;
    Curve25519Key x_public_key;

    if (crypto_sign_ed25519_sk_to_curve25519(x_secret_key.data(), sender_secret_key.data()) != 0
        || crypto_sign_ed25519_pk_to_curve25519(x_public_key.data(), receiver_public_key.data()) != 0) {
        return std::unexpected(CryptoError::KeyConversionFailed);
    }

    Nonce nonce;
    randombytes_buf(nonce.data(), nonce.size());

    Bytes encrypted_message(crypto_box_MACBYTES + data.size());
    if (crypto_box_easy(encrypted_message.data(),
                        data.data(),
                        data.size(),
                        nonce.data(),
                        x_public_key.data(),
                        x_secret_key.data())
        != 0) {
        return std::unexpected(CryptoError::EncryptionFailed);
    }

    Bytes result(nonce.size() + encrypted_message.size());
    std::copy(nonce.begin(), nonce.end(), result.begin());
    std::copy(encrypted_message.begin(), encrypted_message.end(), result.begin() + nonce.size());
    return result;
}

Cryptography::CryptoResult Cryptography::asymmetric_decrypt(const Bytes&      encrypted_data,
                                                            const PrivateKey& receiver_secret_key,
                                                            const PublicKey&  sender_public_key) {
    if (encrypted_data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }

    if (encrypted_data.size() < MIN_ENCRYPTED_SIZE_ASYMMETRIC) {
        return std::unexpected(CryptoError::DataTooShort);
    }

    Nonce nonce;
    std::copy_n(encrypted_data.begin(), crypto_box_NONCEBYTES, nonce.begin());

    Bytes encrypted_message(encrypted_data.begin() + crypto_box_NONCEBYTES, encrypted_data.end());

    Curve25519Key x_secret_key;
    Curve25519Key x_public_key;
    if (crypto_sign_ed25519_sk_to_curve25519(x_secret_key.data(), receiver_secret_key.data()) != 0
        || crypto_sign_ed25519_pk_to_curve25519(x_public_key.data(), sender_public_key.data()) != 0) {
        return std::unexpected(CryptoError::KeyConversionFailed);
    }

    Bytes decrypted_message(encrypted_message.size() - crypto_box_MACBYTES);
    if (crypto_box_open_easy(decrypted_message.data(),
                             encrypted_message.data(),
                             encrypted_message.size(),
                             nonce.data(),
                             x_public_key.data(),
                             x_secret_key.data())
        != 0) {
        return std::unexpected(CryptoError::DecryptionFailed);
    }

    return decrypted_message;
}

Cryptography::CryptoResult Cryptography::asymmetric_encrypt_self(const Bytes&      data,
                                                                 const PrivateKey& self_secret_key,
                                                                 const PublicKey&  self_public_key) {
    Nonce nonce;
    std::copy_n(self_secret_key.begin(),
                std::min(size_t(crypto_box_NONCEBYTES), self_secret_key.size()),
                nonce.begin());
    return asymmetric_encrypt(data, self_secret_key, self_public_key);
}

Cryptography::CryptoResult Cryptography::asymmetric_decrypt_self(const Bytes&      data,
                                                                 const PrivateKey& self_secret_key,
                                                                 const PublicKey&  self_public_key) {
    Nonce nonce;
    std::copy_n(self_secret_key.begin(),
                std::min(size_t(crypto_box_NONCEBYTES), self_secret_key.size()),
                nonce.begin());
    return asymmetric_decrypt(data, self_secret_key, self_public_key);
}

std::expected<bool, FsError> Cryptography::symmetric_encrypt_file(const FsPath&  original_path,
                                                                  const FsPath&  encrypt_path,
                                                                  const KeyPass& key,
                                                                  size_t         block_size) {
    auto valid = validate_encryption_paths(original_path, encrypt_path);
    if (!valid) {
        return valid;
    }

    try {
        std::ifstream orig(original_path.native(), std::ios::binary | std::ios::in);
        std::ofstream encrypt(encrypt_path.native(), std::ios::binary | std::ios::out | std::ios::trunc);
        if (!orig || !encrypt)
            return std::unexpected(FsError::IoError);

        block_size = ((block_size / 8) + 1) * 8;
        std::vector<uint8_t> buffer(block_size);

        while (orig.good()) {
            orig.read(reinterpret_cast<char*>(buffer.data()), block_size);
            auto bytes_read = orig.gcount();
            if (bytes_read <= 0)
                break;

            buffer.resize(bytes_read);
            auto encrypted = symmetric_encrypt(buffer, key);
            if (!encrypted.has_value())
                return std::unexpected(FsError::IoError);

            if (!encrypt.write(reinterpret_cast<const char*>(encrypted->data()), encrypted->size()))
                return std::unexpected(FsError::IoError);
        }

        if (!orig.eof())
            return std::unexpected(FsError::IoError);

        encrypt.flush();
        auto size_result = encrypt_path.file_size();
        if (!size_result) {
            return std::unexpected(size_result.error());
        }
        return *size_result > 0;
    } catch (...) {
        return std::unexpected(FsError::IoError);
    }
}

std::expected<bool, FsError> Cryptography::symmetric_decrypt_file(const FsPath&  encrypt_path,
                                                                  const FsPath&  decrypt_path,
                                                                  const KeyPass& key,
                                                                  size_t         block_size) {
    auto valid = validate_encryption_paths(encrypt_path, decrypt_path);
    if (!valid) {
        return valid;
    }

    try {
        std::ifstream encrypt(encrypt_path.native(), std::ios::binary | std::ios::in);
        std::ofstream decrypt(decrypt_path.native(), std::ios::binary | std::ios::out | std::ios::trunc);
        if (!encrypt || !decrypt)
            return std::unexpected(FsError::IoError);

        block_size                        = ((block_size / 8) + 1) * 8;
        const size_t encrypted_block_size = block_size + crypto_secretbox_MACBYTES + crypto_secretbox_NONCEBYTES;

        std::vector<uint8_t> buffer(encrypted_block_size);
        while (encrypt.good()) {
            encrypt.read(reinterpret_cast<char*>(buffer.data()), encrypted_block_size);
            auto bytes_read = encrypt.gcount();
            if (bytes_read <= 0)
                break;

            if (bytes_read < MIN_ENCRYPTED_SIZE_SYMMETRIC)
                return std::unexpected(FsError::IoError);

            buffer.resize(bytes_read);
            auto decrypted = symmetric_decrypt(buffer, key);
            if (!decrypted.has_value())
                return std::unexpected(FsError::IoError);

            if (!decrypt.write(reinterpret_cast<const char*>(decrypted->data()), decrypted->size()))
                return std::unexpected(FsError::IoError);
        }

        if (!encrypt.eof())
            return std::unexpected(FsError::IoError);

        decrypt.flush();
        auto size_result = decrypt_path.file_size();
        if (!size_result) {
            return std::unexpected(size_result.error());
        }
        return *size_result > 0;
    } catch (...) {
        return std::unexpected(FsError::IoError);
    }
}

std::expected<bool, FsError> Cryptography::symmetric_encrypt_file_password(const FsPath&      original_path,
                                                                           const FsPath&      encrypt_path,
                                                                           const std::string& password,
                                                                           size_t             block_size) {
    auto key_result = key_from_password(password);
    if (!key_result.has_value()) {
        return std::unexpected(FsError::IoError);
    }
    return symmetric_encrypt_file(original_path, encrypt_path, *key_result, block_size);
}

std::expected<bool, FsError> Cryptography::symmetric_decrypt_file_password(const FsPath&      encrypt_path,
                                                                           const FsPath&      decrypt_path,
                                                                           const std::string& password,
                                                                           size_t             block_size) {
    auto key_result = key_from_password(password);
    if (!key_result.has_value()) {
        return std::unexpected(FsError::IoError);
    }
    return symmetric_decrypt_file(encrypt_path, decrypt_path, *key_result, block_size);
}

std::expected<bool, FsError> Cryptography::asymmetric_encrypt_file(const FsPath&     input_path,
                                                                   const FsPath&     output_path,
                                                                   const PrivateKey& sender_secret_key,
                                                                   const PublicKey&  receiver_public_key,
                                                                   size_t            block_size) {
    auto valid = validate_encryption_paths(input_path, output_path);
    if (!valid) {
        return valid;
    }

    try {
        std::ifstream in(input_path.native(), std::ios::binary | std::ios::in);
        std::ofstream out(output_path.native(), std::ios::binary | std::ios::out | std::ios::trunc);
        if (!in || !out)
            return std::unexpected(FsError::IoError);

        block_size = ((block_size / 8) + 1) * 8;
        std::vector<uint8_t> buffer(block_size);

        while (in.good()) {
            in.read(reinterpret_cast<char*>(buffer.data()), block_size);
            auto bytes_read = in.gcount();
            if (bytes_read <= 0)
                break;

            buffer.resize(bytes_read);
            auto encrypted = asymmetric_encrypt(buffer, sender_secret_key, receiver_public_key);
            if (!encrypted.has_value())
                return std::unexpected(FsError::IoError);

            if (!out.write(reinterpret_cast<const char*>(encrypted->data()), encrypted->size()))
                return std::unexpected(FsError::IoError);
        }

        if (!in.eof())
            return std::unexpected(FsError::IoError);

        out.flush();
        auto size_result = output_path.file_size();
        if (!size_result) {
            return std::unexpected(size_result.error());
        }
        return *size_result > 0;
    } catch (...) {
        return std::unexpected(FsError::IoError);
    }
}

std::expected<bool, FsError> Cryptography::asymmetric_decrypt_file(const FsPath&     input_path,
                                                                   const FsPath&     output_path,
                                                                   const PrivateKey& receiver_secret_key,
                                                                   const PublicKey&  sender_public_key,
                                                                   size_t            block_size) {
    auto valid = validate_encryption_paths(input_path, output_path);
    if (!valid) {
        return valid;
    }

    try {
        std::ifstream in(input_path.native(), std::ios::binary | std::ios::in);
        std::ofstream out(output_path.native(), std::ios::binary | std::ios::out | std::ios::trunc);
        if (!in || !out)
            return std::unexpected(FsError::IoError);

        block_size                        = ((block_size / 8) + 1) * 8;
        const size_t encrypted_block_size = block_size + crypto_box_MACBYTES + crypto_box_NONCEBYTES;

        std::vector<uint8_t> encrypted(encrypted_block_size);
        while (in.good()) {
            in.read(reinterpret_cast<char*>(encrypted.data()), encrypted_block_size);
            auto bytes_read = in.gcount();
            if (bytes_read <= 0)
                break;

            if (bytes_read < MIN_ENCRYPTED_SIZE_ASYMMETRIC)
                return std::unexpected(FsError::IoError);

            encrypted.resize(bytes_read);
            auto decrypted = asymmetric_decrypt(encrypted, receiver_secret_key, sender_public_key);
            if (!decrypted.has_value())
                return std::unexpected(FsError::IoError);

            if (!out.write(reinterpret_cast<const char*>(decrypted->data()), decrypted->size()))
                return std::unexpected(FsError::IoError);
        }

        if (!in.eof())
            return std::unexpected(FsError::IoError);

        out.flush();
        auto size_result = output_path.file_size();
        if (!size_result) {
            return std::unexpected(size_result.error());
        }
        return *size_result > 0;
    } catch (...) {
        return std::unexpected(FsError::IoError);
    }
}

std::expected<bool, FsError> Cryptography::asymmetric_encrypt_self_file(const FsPath&     input_path,
                                                                        const FsPath&     output_path,
                                                                        const PrivateKey& self_secret_key,
                                                                        const PublicKey&  self_public_key,
                                                                        size_t            block_size) {
    return asymmetric_encrypt_file(input_path, output_path, self_secret_key, self_public_key, block_size);
}

std::expected<bool, FsError> Cryptography::asymmetric_decrypt_self_file(const FsPath&     input_path,
                                                                        const FsPath&     output_path,
                                                                        const PrivateKey& self_secret_key,
                                                                        const PublicKey&  self_public_key,
                                                                        size_t            block_size) {
    return asymmetric_decrypt_file(input_path, output_path, self_secret_key, self_public_key, block_size);
}

std::expected<bool, FsError> Cryptography::validate_encryption_paths(const FsPath& input_path,
                                                                     const FsPath& output_path) {
    auto in_exists = input_path.exists();
    if (!in_exists)
        return in_exists;

    auto in_is_file = input_path.is_regular_file();
    if (!in_is_file)
        return in_is_file;

    auto in_readable = input_path.has_read_permission();
    if (!in_readable)
        return in_readable;

    auto out_parent = output_path.parent_path();
    if (!out_parent.has_value())
        return std::unexpected(out_parent.error());

    auto parent_exists = out_parent->exists();
    if (!parent_exists)
        return parent_exists;

    auto parent_is_dir = out_parent->is_directory();
    if (!parent_is_dir)
        return std::unexpected(FsError::ParentNotDirectory);

    if (output_path == input_path)
        return std::unexpected(FsError::ValidationError);

    return true;
}
