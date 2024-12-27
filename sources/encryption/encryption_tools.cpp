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

#include <expected>
#include "encryption/encryption_tools.h"

KeyBytes Cryptography::keygen() {
    KeyBytes sk;
    crypto_secretbox_keygen(sk.data());
    // string skey = std::string(sk.begin(), sk.end());
    // skey.erase(--skey.end());
    return sk;
}

KeyPass Cryptography::key_from_password(const std::string &password, const Salt &salt) {
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
        eFatal("Incorrect getKeyFromPass");
    }
    // string skey = std::string(key.begin(), key.end());
    // skey.erase(--skey.end());
    return key;
}

Signature Cryptography::sign(const Bytes &data, const PrivateKey &secret_key) {
    if (data.empty() || Utils::is_container_empty(secret_key)) {
        eFatal("[SecretKey::sign] data or secret is empty. data: {}, secret: {}", data, secret_key);
    }

    Signature sig;
    crypto_sign_detached(sig.data(), NULL, data.data(), data.size(), secret_key.data());
    return sig;
}

bool Cryptography::verify(const Bytes &data, const PublicKey &public_key, const Signature &signature) {
    if (data.empty() || Utils::is_container_empty(public_key) || Utils::is_container_empty(signature)) {
        qCritical().noquote().nospace()
            << "[SecretKey::verify] data or secret is empty. data: '" << data << "', public: '"
            << public_key.data() << "', signature: '" << signature.data() << "'";
        return false;
    }

    int res = crypto_sign_verify_detached(signature.data(), data.data(), data.size(), public_key.data());
    return res == 0;
}

Bytes Cryptography::symmetric_encrypt(const Bytes &data, const KeyPass &secret_key) {
    if (data.empty() || Utils::is_container_empty(secret_key)) {
        eFatal("[SecretKey::encrypt] data or secret is empty. data: {}, secret: {}", data, secret_key);
    }

    unsigned long long enc_size = crypto_secretbox_MACBYTES + data.size();
    Bytes              encrypted(enc_size);

    Nonce nonce;
    randombytes_buf(nonce.data(), nonce.size());

    int r = crypto_secretbox_easy(encrypted.data(), data.data(), data.size(), nonce.data(), secret_key.data());

    if (r != 0) {
        eLog("[SecretKey::encrypt] Encryption failed");
        return Bytes();
    }

    encrypted.insert(encrypted.begin(), nonce.begin(), nonce.end());
    return encrypted;
}

Bytes Cryptography::symmetric_decrypt(const Bytes &encrypted_data, const KeyPass &secret_key) {
    if (encrypted_data.empty() || Utils::is_container_empty(secret_key)) {
        eFatal("[SecretKey::decrypt] data or secret is empty. data: {}, secret: {}", encrypted_data, secret_key);
    }

    const size_t minimum_size = crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES;
    if (encrypted_data.size() < minimum_size) {
        eFatal("[SecretKey::decrypt] Encrypted data too short");
    }

    Nonce nonce;
    std::copy_n(encrypted_data.begin(), crypto_secretbox_NONCEBYTES, nonce.begin());

    const Bytes encrypted_message(encrypted_data.begin() + crypto_secretbox_NONCEBYTES, encrypted_data.end());

    if (encrypted_message.size() < crypto_secretbox_MACBYTES) {
        eFatal("[SecretKey::decrypt] Incorrect msg size");
    }

    Bytes decrypted_message(encrypted_message.size() - crypto_secretbox_MACBYTES);

    int r = crypto_secretbox_open_easy(decrypted_message.data(),
                                       encrypted_message.data(),
                                       encrypted_message.size(),
                                       nonce.data(),
                                       secret_key.data());

    if (r != 0) {
        eLog("[SecretKey::decrypt] Decryption failed");
        return Bytes();
    }

    return decrypted_message;
}

std::string Cryptography::symmetric_encrypt(const std::string &data, const KeyPass &secret_key) {
    auto res = symmetric_encrypt(ByteArray(data).toBytes(), secret_key);
    return ByteArray(res).toString();
}

std::string Cryptography::symmetric_decrypt(const std::string &data, const KeyPass &secret_key) {
    auto res = symmetric_decrypt(ByteArray(data).toBytes(), secret_key);
    return ByteArray(res).toString();
}

Bytes Cryptography::symmetric_encrypt_password(const Bytes &data, const std::string &password) {
    auto key = key_from_password(password);
    return symmetric_encrypt(data, key);
}

Bytes Cryptography::symmetric_decrypt_password(const Bytes &data, const std::string &password) {
    auto key = key_from_password(password);
    return symmetric_decrypt(data, key);
}

std::pair<PrivateKey, PublicKey> Cryptography::asymmetric_create_pair() {
    PrivateKey sk;
    PublicKey  pk;
    crypto_sign_keypair(pk.data(), sk.data());
    return { sk, pk };
}

Cryptography::CryptoResult Cryptography::asymmetric_encrypt(const Bytes                   &data,
                                                            const PrivateKey              &sender_secret_key,
                                                            const PublicKey               &receiver_public_key,
                                                            const Nonce                   &nonce,
                                                            const Cryptography::NonceWrite nonce_write) {
    if (data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }

    Curve25519Key x_secret_key;
    Curve25519Key x_public_key;
    int           res1 = crypto_sign_ed25519_sk_to_curve25519(x_secret_key.data(), sender_secret_key.data());
    int           res2 = crypto_sign_ed25519_pk_to_curve25519(x_public_key.data(), receiver_public_key.data());

    if (res1 != 0 || res2 != 0) {
        return std::unexpected(CryptoError::KeyConversionFailed);
    }

    Nonce working_nonce;
    bool  should_generate_nonce = Utils::is_container_empty(nonce);
    if (!should_generate_nonce) {
        working_nonce = nonce;
    } else {
        randombytes_buf(working_nonce.data(), working_nonce.size());
    }

    Bytes encrypted_message(crypto_box_MACBYTES + data.size());
    int   res = crypto_box_easy(encrypted_message.data(),
                              data.data(),
                              data.size(),
                              working_nonce.data(),
                              x_public_key.data(),
                              x_secret_key.data());
    if (res != 0) {
        return std::unexpected(CryptoError::EncryptionFailed);
    }

    if (nonce_write == NonceWrite::Enable && should_generate_nonce) {
        Bytes result(working_nonce.size() + encrypted_message.size());
        std::copy(working_nonce.begin(), working_nonce.end(), result.begin());
        std::copy(encrypted_message.begin(), encrypted_message.end(), result.begin() + working_nonce.size());
        return result;
    }

    return encrypted_message;
}

Cryptography::CryptoResult Cryptography::asymmetric_decrypt(const Bytes                   &encrypted_data,
                                                            const PrivateKey              &receiver_secret_key,
                                                            const PublicKey               &sender_public_key,
                                                            const Nonce                   &nonce,
                                                            const Cryptography::NonceWrite nonce_write) {
    if (encrypted_data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }

    Nonce working_nonce;
    Bytes encrypted_message;
    bool  external_nonce = !Utils::is_container_empty(nonce);

    if (external_nonce) {
        working_nonce     = nonce;
        encrypted_message = encrypted_data;
    } else {
        if (nonce_write == NonceWrite::Enable) {
            if (encrypted_data.size() < crypto_box_NONCEBYTES) {
                return std::unexpected(CryptoError::DataTooShort);
            }
            std::copy_n(encrypted_data.begin(), crypto_box_NONCEBYTES, working_nonce.begin());
            encrypted_message = Bytes(encrypted_data.begin() + crypto_box_NONCEBYTES, encrypted_data.end());
        } else {
            return std::unexpected(CryptoError::NoNonceProvided);
        }
    }

    if (encrypted_message.size() < crypto_box_MACBYTES) {
        return std::unexpected(CryptoError::DataTooShort);
    }

    Curve25519Key x_secret_key;
    Curve25519Key x_public_key;
    int           res1 = crypto_sign_ed25519_sk_to_curve25519(x_secret_key.data(), receiver_secret_key.data());
    int           res2 = crypto_sign_ed25519_pk_to_curve25519(x_public_key.data(), sender_public_key.data());

    if (res1 != 0 || res2 != 0) {
        return std::unexpected(CryptoError::KeyConversionFailed);
    }

    Bytes decrypted_message(encrypted_message.size() - crypto_box_MACBYTES);
    int   res = crypto_box_open_easy(decrypted_message.data(),
                                   encrypted_message.data(),
                                   encrypted_message.size(),
                                   working_nonce.data(),
                                   x_public_key.data(),
                                   x_secret_key.data());
    if (res != 0) {
        return std::unexpected(CryptoError::DecryptionFailed);
    }

    return decrypted_message;
}

Cryptography::CryptoResult Cryptography::asymmetric_encrypt_self(const Bytes      &data,
                                                                 const PrivateKey &self_secret_key,
                                                                 const PublicKey  &self_public_key) {
    Nonce nonce;
    std::copy_n(self_secret_key.begin(),
                std::min(size_t(crypto_box_NONCEBYTES), self_secret_key.size()),
                nonce.begin());

    return asymmetric_encrypt(data, self_secret_key, self_public_key, nonce, Cryptography::NonceWrite::Disable);
}

Cryptography::CryptoResult Cryptography::asymmetric_decrypt_self(const Bytes      &data,
                                                                 const PrivateKey &self_secret_key,
                                                                 const PublicKey  &self_public_key) {
    Nonce nonce;
    std::copy_n(self_secret_key.begin(),
                std::min(size_t(crypto_box_NONCEBYTES), self_secret_key.size()),
                nonce.begin());

    return asymmetric_decrypt(data, self_secret_key, self_public_key, nonce, Cryptography::NonceWrite::Disable);
}

std::expected<bool, FsError> validate_file(const FsPath &path) {
    auto exists = path.exists();
    if (!exists || !*exists)
        return exists;
    auto is_file = path.is_regular_file();
    if (!is_file)
        return is_file;
    return *is_file;
}
std::expected<bool, FsError> Cryptography::symmetric_encrypt_file(const FsPath   &original_path,
                                                                  const FsPath   &encrypt_path,
                                                                  const KeyBytes &key,
                                                                  size_t          block_size) {
    auto valid = validate_file(original_path);
    if (!valid)
        return valid;

    std::ifstream orig(original_path.native(), std::ios::binary | std::ios::in);
    std::ofstream encrypt(encrypt_path.native(), std::ios::binary | std::ios::out | std::ios::trunc);
    if (!orig || !encrypt)
        return std::unexpected(FsError::IoError);

    block_size = ((block_size / 8) + 1) * 8;
    auto rkey =
        Cryptography::key_from_password(std::string(reinterpret_cast<const char *>(key.data()), key.size()));
    if (rkey.empty()) {
        return std::unexpected(FsError::IoError);
    }

    std::vector<uint8_t> buffer(block_size);
    while (orig.good()) {
        orig.read(reinterpret_cast<char *>(buffer.data()), block_size);
        auto bytes_read = orig.gcount();
        if (bytes_read <= 0)
            break;

        buffer.resize(bytes_read);
        auto encrypted = Cryptography::symmetric_encrypt(buffer, rkey);
        if (encrypted.empty()) {
            return std::unexpected(FsError::IoError);
        }

        if (!encrypt.write(reinterpret_cast<const char *>(encrypted.data()), encrypted.size())) {
            return std::unexpected(FsError::IoError);
        }
    }

    if (!orig.eof()) {
        return std::unexpected(FsError::IoError);
    }

    encrypt.flush();
    auto size_result = encrypt_path.file_size();
    if (!size_result)
        return std::unexpected(size_result.error());
    return *size_result > 0;
}

std::expected<bool, FsError> Cryptography::symmetric_decrypt_file(const FsPath   &encrypt_path,
                                                                  const FsPath   &decrypt_path,
                                                                  const KeyBytes &key,
                                                                  size_t          block_size) {
    auto valid = validate_file(encrypt_path);
    if (!valid)
        return valid;

    std::ifstream encrypt(encrypt_path.native(), std::ios::binary | std::ios::in);
    std::ofstream decrypt(decrypt_path.native(), std::ios::binary | std::ios::out | std::ios::trunc);
    if (!encrypt || !decrypt)
        return std::unexpected(FsError::IoError);

    block_size                        = ((block_size / 8) + 1) * 8;
    const size_t encrypted_block_size = block_size + crypto_secretbox_MACBYTES;

    auto rkey =
        Cryptography::key_from_password(std::string(reinterpret_cast<const char *>(key.data()), key.size()));
    if (rkey.empty()) {
        return std::unexpected(FsError::IoError);
    }

    std::vector<uint8_t> buffer(encrypted_block_size);
    while (encrypt.good()) {
        encrypt.read(reinterpret_cast<char *>(buffer.data()), encrypted_block_size);
        auto bytes_read = encrypt.gcount();
        if (bytes_read <= 0)
            break;

        if (bytes_read < crypto_secretbox_MACBYTES) {
            return std::unexpected(FsError::IoError);
        }

        if (bytes_read != encrypted_block_size) {
            buffer.resize(bytes_read);
        }

        auto decrypted = Cryptography::symmetric_decrypt(buffer, rkey);
        if (decrypted.empty()) {
            return std::unexpected(FsError::IoError);
        }

        if (!decrypt.write(reinterpret_cast<const char *>(decrypted.data()), decrypted.size())) {
            return std::unexpected(FsError::IoError);
        }
    }

    if (!encrypt.eof()) {
        return std::unexpected(FsError::IoError);
    }

    decrypt.flush();
    auto size_result = decrypt_path.file_size();
    if (!size_result)
        return std::unexpected(size_result.error());
    return *size_result > 0;
}

std::expected<bool, FsError> Cryptography::asymmetric_encrypt_file(const FsPath     &input_path,
                                                                   const FsPath     &output_path,
                                                                   const PrivateKey &sender_secret_key,
                                                                   const PublicKey  &receiver_public_key,
                                                                   const Nonce      &nonce,
                                                                   size_t            block_size) {
    auto valid = validate_file(input_path);
    if (!valid)
        return valid;

    std::ifstream in(input_path.native(), std::ios::binary | std::ios::in);
    std::ofstream out(output_path.native(), std::ios::binary | std::ios::out | std::ios::trunc);
    if (!in || !out)
        return std::unexpected(FsError::IoError);

    block_size = ((block_size / 8) + 1) * 8;
    std::vector<uint8_t> buffer(block_size);

    while (in.good()) {
        in.read(reinterpret_cast<char *>(buffer.data()), block_size);
        auto bytes_read = in.gcount();
        if (bytes_read <= 0)
            break;

        buffer.resize(bytes_read);
        auto encrypted = Cryptography::asymmetric_encrypt(buffer,
                                                          sender_secret_key,
                                                          receiver_public_key,
                                                          nonce,
                                                          NonceWrite::Disable);
        if (!encrypted.has_value()) {
            return std::unexpected(FsError::IoError);
        }
        if (encrypted->empty()) {
            return std::unexpected(FsError::IoError);
        }

        if (!out.write(reinterpret_cast<char *>(encrypted->data()), encrypted->size())) {
            return std::unexpected(FsError::IoError);
        }
    }

    if (!in.eof()) {
        return std::unexpected(FsError::IoError);
    }

    out.flush();
    auto size_result = output_path.file_size();
    if (!size_result.has_value())
        return std::unexpected(size_result.error());
    return *size_result > 0;
}

std::expected<bool, FsError> Cryptography::asymmetric_decrypt_file(const FsPath     &input_path,
                                                                   const FsPath     &output_path,
                                                                   const PrivateKey &receiver_secret_key,
                                                                   const PublicKey  &sender_public_key,
                                                                   const Nonce      &nonce,
                                                                   size_t            block_size) {
    auto valid = validate_file(input_path);
    if (!valid)
        return valid;

    std::ifstream in(input_path.native(), std::ios::binary | std::ios::in);
    std::ofstream out(output_path.native(), std::ios::binary | std::ios::out | std::ios::trunc);
    if (!in || !out)
        return std::unexpected(FsError::IoError);

    block_size                        = ((block_size / 8) + 1) * 8;
    const size_t encrypted_block_size = block_size + crypto_box_MACBYTES;

    std::vector<uint8_t> encrypted(encrypted_block_size);
    while (in.good()) {
        in.read(reinterpret_cast<char *>(encrypted.data()), encrypted_block_size);
        auto bytes_read = in.gcount();
        if (bytes_read <= 0)
            break;

        if (bytes_read < crypto_box_MACBYTES) {
            return std::unexpected(FsError::IoError);
        }

        if (bytes_read != encrypted_block_size) {
            encrypted.resize(bytes_read);
        }

        auto decrypted = Cryptography::asymmetric_decrypt(encrypted,
                                                          receiver_secret_key,
                                                          sender_public_key,
                                                          nonce,
                                                          NonceWrite::Disable);
        if (!decrypted.has_value()) {
            return std::unexpected(FsError::IoError);
        }
        if (decrypted->empty()) {
            return std::unexpected(FsError::IoError);
        }

        if (!out.write(reinterpret_cast<char *>(decrypted->data()), decrypted->size())) {
            return std::unexpected(FsError::IoError);
        }
    }

    if (!in.eof()) {
        return std::unexpected(FsError::IoError);
    }

    out.flush();
    auto size_result = output_path.file_size();
    if (!size_result)
        return std::unexpected(size_result.error());
    return *size_result > 0;
}

std::expected<bool, FsError> Cryptography::asymmetric_encrypt_self_file(const FsPath     &input_path,
                                                                        const FsPath     &output_path,
                                                                        const PrivateKey &self_secret_key,
                                                                        const PublicKey  &self_public_key,
                                                                        size_t            block_size) {
    Nonce nonce;
    std::copy_n(self_secret_key.begin(),
                std::min(size_t(crypto_box_NONCEBYTES), self_secret_key.size()),
                nonce.begin());

    return asymmetric_encrypt_file(input_path, output_path, self_secret_key, self_public_key, nonce);
}

std::expected<bool, FsError> Cryptography::asymmetric_decrypt_self_file(const FsPath     &input_path,
                                                                        const FsPath     &output_path,
                                                                        const PrivateKey &self_secret_key,
                                                                        const PublicKey  &self_public_key,
                                                                        size_t            block_size) {
    Nonce nonce;
    std::copy_n(self_secret_key.begin(),
                std::min(size_t(crypto_box_NONCEBYTES), self_secret_key.size()),
                nonce.begin());

    return asymmetric_decrypt_file(input_path, output_path, self_secret_key, self_public_key, nonce);
}
