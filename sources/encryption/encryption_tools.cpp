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

#include <fstream>

#include "core/container.h"

#include <bip3x/bip3x_mnemonic.h>

using Cryptography::CryptoError;

namespace {
    constexpr std::array<std::uint8_t, 10> PASSWORD_ENVELOPE_PREFIX { 'E', 'C', 'P', '2', 2, 2, 4, 0, 0, 0 };

    std::expected<KeyPass, CryptoError> envelope_key(const std::string& password, const Salt& salt) {
        KeyPass key;
        if (password.empty()
            || crypto_pwhash(key.data(),
                             key.size(),
                             password.data(),
                             password.size(),
                             salt.data(),
                             2,
                             64 * 1024 * 1024,
                             crypto_pwhash_ALG_ARGON2ID13)
                   != 0) {
            return std::unexpected(CryptoError::KeyConversionFailed);
        }
        return key;
    }

    std::expected<Curve25519Key, CryptoError> curve_public_key(const PublicKey& public_key) {
        thread_local std::map<PublicKey, Curve25519Key> converted_keys;
        const auto                                      found = converted_keys.find(public_key);
        if (found != converted_keys.end()) {
            return found->second;
        }
        Curve25519Key converted;
        if (crypto_sign_ed25519_pk_to_curve25519(converted.data(), public_key.data()) != 0) {
            return std::unexpected(CryptoError::KeyConversionFailed);
        }
        // Conversion is immutable for an exact public key. No secret is cached.
        if (converted_keys.size() >= 256) {
            converted_keys.clear();
        }
        converted_keys.emplace(public_key, converted);
        return converted;
    }

    bool validate_file_basic(const FsPath& path) {
        auto exists = path.exists();
        if (!exists)
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
    if (ExtraChain::Core::all_zero(salt)) {
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
    if (ExtraChain::Core::all_zero(secret_key)) {
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
    if (ExtraChain::Core::all_zero(public_key)) {
        return std::unexpected(CryptoError::EmptyKey);
    }
    if (ExtraChain::Core::all_zero(signature)) {
        return std::unexpected(CryptoError::EmptySign);
    }

    int res = crypto_sign_verify_detached(signature.data(), data.data(), data.size(), public_key.data());
    return res == 0;
}

Cryptography::CryptoResult Cryptography::symmetric_encrypt(const Bytes&   data,
                                                           const KeyPass& secret_key,
                                                           bool           nonce_from_key) {
    if (data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }
    if (ExtraChain::Core::all_zero(secret_key)) {
        return std::unexpected(CryptoError::EmptyKey);
    }

    Nonce nonce;

    if (nonce_from_key) {
        std::copy(secret_key.begin(), secret_key.begin() + nonce.size(), nonce.begin());
    } else {
        randombytes_buf(nonce.data(), nonce.size());
    }

    const auto nonce_size = nonce_from_key ? std::size_t { 0 } : nonce.size();
    Bytes      result(nonce_size + crypto_secretbox_MACBYTES + data.size());
    if (!nonce_from_key) {
        std::copy(nonce.begin(), nonce.end(), result.begin());
    }

    if (crypto_secretbox_easy(result.data() + nonce_size,
                              data.data(),
                              data.size(),
                              nonce.data(),
                              secret_key.data())
        != 0) {
        return std::unexpected(CryptoError::EncryptionFailed);
    }

    return result;
}

Cryptography::CryptoResult Cryptography::symmetric_decrypt(const Bytes&   encrypted_data,
                                                           const KeyPass& secret_key,
                                                           bool           nonce_from_key) {
    if (encrypted_data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }

    if (ExtraChain::Core::all_zero(secret_key)) {
        return std::unexpected(CryptoError::EmptyKey);
    }

    if (encrypted_data.size() < (nonce_from_key ? crypto_secretbox_MACBYTES : MIN_ENCRYPTED_SIZE_SYMMETRIC)) {
        return std::unexpected(CryptoError::DataTooShort);
    }

    Nonce nonce;
    if (nonce_from_key) {
        std::copy(secret_key.begin(), secret_key.begin() + nonce.size(), nonce.begin());
    } else {
        std::copy_n(encrypted_data.begin(), crypto_secretbox_NONCEBYTES, nonce.begin());
    }

    const auto encrypted_offset = nonce_from_key ? std::size_t { 0 } : std::size_t { crypto_secretbox_NONCEBYTES };
    const auto encrypted_size   = encrypted_data.size() - encrypted_offset;
    Bytes      decrypted_message(encrypted_size - crypto_secretbox_MACBYTES);

    if (crypto_secretbox_open_easy(decrypted_message.data(),
                                   encrypted_data.data() + encrypted_offset,
                                   encrypted_size,
                                   nonce.data(),
                                   secret_key.data())
        != 0) {
        return std::unexpected(CryptoError::DecryptionFailed);
    }

    return decrypted_message;
}

Cryptography::CryptoResult Cryptography::symmetric_encrypt_password(const Bytes&       data,
                                                                    const std::string& password) {
    if (data.empty() || password.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }
    Salt salt;
    randombytes_buf(salt.data(), salt.size());
    auto key = envelope_key(password, salt);
    if (!key.has_value()) {
        return std::unexpected(key.error());
    }
    auto encrypted = symmetric_encrypt(data, key.value());
    sodium_memzero(key.value().data(), key.value().size());
    if (!encrypted.has_value()) {
        return encrypted;
    }
    Bytes result(PASSWORD_ENVELOPE_PREFIX.begin(), PASSWORD_ENVELOPE_PREFIX.end());
    result.insert(result.end(), salt.begin(), salt.end());
    result.insert(result.end(), encrypted.value().begin(), encrypted.value().end());
    return result;
}

Cryptography::CryptoResult Cryptography::symmetric_decrypt_password(const Bytes&       data,
                                                                    const std::string& password,
                                                                    bool               nonce_from_key) {
    if (data.empty() || password.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }
    constexpr std::size_t header_size = PASSWORD_ENVELOPE_PREFIX.size() + crypto_pwhash_SALTBYTES;
    const bool            modern =
        data.size() >= 4 && std::equal(data.begin(), data.begin() + 4, PASSWORD_ENVELOPE_PREFIX.begin());
    if (modern) {
        if (data.size() < header_size + MIN_ENCRYPTED_SIZE_SYMMETRIC
            || !std::equal(PASSWORD_ENVELOPE_PREFIX.begin(), PASSWORD_ENVELOPE_PREFIX.end(), data.begin())) {
            return std::unexpected(CryptoError::DecryptionFailed);
        }
        Salt salt;
        std::copy_n(data.begin() + PASSWORD_ENVELOPE_PREFIX.size(), salt.size(), salt.begin());
        auto key = envelope_key(password, salt);
        if (!key.has_value()) {
            return std::unexpected(key.error());
        }
        auto result = symmetric_decrypt(Bytes(data.begin() + header_size, data.end()), key.value());
        sodium_memzero(key.value().data(), key.value().size());
        return result;
    }
    auto key = key_from_password(password);
    if (!key.has_value()) {
        return std::unexpected(key.error());
    }
    auto result = symmetric_decrypt(data, key.value(), nonce_from_key);
    sodium_memzero(key.value().data(), key.value().size());
    return result;
}

std::pair<PrivateKey, PublicKey> Cryptography::asymmetric_create_pair() {
    PrivateKey sk;
    PublicKey  pk;
    crypto_sign_keypair(pk.data(), sk.data());
    return { sk, pk };
}

MasterSeed Cryptography::generate_seed() {
    MasterSeed master_seed;
    randombytes_buf(master_seed.data(), master_seed.size());
    return master_seed;
}

std::pair<PrivateKey, PublicKey> Cryptography::asymmetric_from_seed(const MasterSeed& master_seed,
                                                                    std::uint32_t     index) {
    std::array<std::uint8_t, 32> derived_seed;

    // Prepare derivation data: master_seed || index (4 bytes, big-endian)
    std::array<std::uint8_t, 36> derivation_data;

    // Copy master seed
    std::memcpy(derivation_data.data(), master_seed.data(), 32);

    // Append index as 4 bytes (big-endian)
    derivation_data[32] = (index >> 24) & 0xFF;
    derivation_data[33] = (index >> 16) & 0xFF;
    derivation_data[34] = (index >> 8) & 0xFF;
    derivation_data[35] = index & 0xFF;

    // Derive seed using BLAKE2b
    crypto_generichash(derived_seed.data(),
                       derived_seed.size(),
                       derivation_data.data(),
                       derivation_data.size(),
                       nullptr,
                       0);

    // Generate Ed25519 keypair
    PrivateKey private_key;
    PublicKey  public_key;
    crypto_sign_seed_keypair(public_key.data(), private_key.data(), derived_seed.data());

    return { private_key, public_key };
}

std::pair<PrivateKey, PublicKey> Cryptography::asymmetric_from_seed(const MasterSeed&  master_seed,
                                                                    const std::string& label) {
    std::array<std::uint8_t, 32> derived_seed;

    std::vector<std::uint8_t> derivation_data;
    derivation_data.reserve(master_seed.size() + label.size());
    derivation_data.insert(derivation_data.end(), master_seed.begin(), master_seed.end());
    derivation_data.insert(derivation_data.end(), label.begin(), label.end());

    crypto_generichash(derived_seed.data(),
                       derived_seed.size(),
                       derivation_data.data(),
                       derivation_data.size(),
                       nullptr,
                       0);

    PrivateKey private_key;
    PublicKey  public_key;
    crypto_sign_seed_keypair(public_key.data(), private_key.data(), derived_seed.data());

    return { private_key, public_key };
}

PublicKey Cryptography::get_public_from_private(const PrivateKey& private_key) {
    PublicKey public_key;
    std::copy(private_key.begin() + 32, private_key.end(), public_key.begin());
    return public_key;
}

std::vector<std::string> Cryptography::create_mnemonic(const MasterSeed& master_seed) {
    auto result = bip3x::bip3x_mnemonic::encode_bytes(master_seed.data(), "en", BIP3X_ENTROPY_LEN_256);
    return result.words;
}

std::expected<MasterSeed, Cryptography::MnemonicError> Cryptography::restore_seed_from_mnemonic(
    const std::string& mnemonic) {
    if (mnemonic.empty()) {
        return std::unexpected(Cryptography::MnemonicError::Empty);
    }

    if (!validate_mnemonic(mnemonic)) {
        return std::unexpected(Cryptography::MnemonicError::Validate);
    }

    auto decoded = bip3x::bip3x_mnemonic::decode_mnemonic(mnemonic.c_str(), "en", BIP3X_ENTROPY_LEN_256);

    MasterSeed master_seed;
    std::copy(decoded.begin(), decoded.end(), master_seed.begin());

    return master_seed;
}

bool Cryptography::validate_mnemonic(const std::string& mnemonic) {
    if (mnemonic.empty()) {
        return false;
    }

    return bip3x::bip3x_mnemonic::validate_words("en", mnemonic.c_str());
}

Cryptography::CryptoResult Cryptography::asymmetric_encrypt(const Bytes&      data,
                                                            const PrivateKey& sender_secret_key,
                                                            const PublicKey&  receiver_public_key) {
    if (data.empty()) {
        return std::unexpected(CryptoError::EmptyData);
    }

    Curve25519Key x_secret_key;
    if (crypto_sign_ed25519_sk_to_curve25519(x_secret_key.data(), sender_secret_key.data()) != 0) {
        return std::unexpected(CryptoError::KeyConversionFailed);
    }
    const auto x_public_key = curve_public_key(receiver_public_key);
    if (!x_public_key.has_value()) {
        return std::unexpected(CryptoError::KeyConversionFailed);
    }

    Nonce nonce;
    randombytes_buf(nonce.data(), nonce.size());

    Bytes result(nonce.size() + crypto_box_MACBYTES + data.size());
    std::copy(nonce.begin(), nonce.end(), result.begin());
    if (crypto_box_easy(result.data() + nonce.size(),
                        data.data(),
                        data.size(),
                        nonce.data(),
                        x_public_key.value().data(),
                        x_secret_key.data())
        != 0) {
        return std::unexpected(CryptoError::EncryptionFailed);
    }

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

    Curve25519Key x_secret_key;
    if (crypto_sign_ed25519_sk_to_curve25519(x_secret_key.data(), receiver_secret_key.data()) != 0) {
        return std::unexpected(CryptoError::KeyConversionFailed);
    }
    const auto x_public_key = curve_public_key(sender_public_key);
    if (!x_public_key.has_value()) {
        return std::unexpected(CryptoError::KeyConversionFailed);
    }

    const auto encrypted_size = encrypted_data.size() - crypto_box_NONCEBYTES;
    Bytes      decrypted_message(encrypted_size - crypto_box_MACBYTES);
    if (crypto_box_open_easy(decrypted_message.data(),
                             encrypted_data.data() + crypto_box_NONCEBYTES,
                             encrypted_size,
                             nonce.data(),
                             x_public_key.value().data(),
                             x_secret_key.data())
        != 0) {
        return std::unexpected(CryptoError::DecryptionFailed);
    }

    return decrypted_message;
}

Cryptography::CryptoResult Cryptography::asymmetric_encrypt_self(const Bytes&      data,
                                                                 const PrivateKey& self_secret_key,
                                                                 const PublicKey&  self_public_key) {
    return asymmetric_encrypt(data, self_secret_key, self_public_key);
}

Cryptography::CryptoResult Cryptography::asymmetric_decrypt_self(const Bytes&      data,
                                                                 const PrivateKey& self_secret_key,
                                                                 const PublicKey&  self_public_key) {
    return asymmetric_decrypt(data, self_secret_key, self_public_key);
}

std::expected<bool, FsError> Cryptography::validate_encryption_paths(const FsPath& input_path,
                                                                     const FsPath& output_path) {
    if (!input_path.exists()) {
        return std::unexpected(FsError::ValidationError);
    }
    const auto regular = input_path.is_regular_file();
    if (!regular.has_value() || !regular.value()) {
        return std::unexpected(FsError::ValidationError);
    }
    const auto readable = input_path.has_read_permission();
    if (!readable.has_value() || !readable.value()) {
        return std::unexpected(FsError::ValidationError);
    }
    const auto absolute = output_path.absolute();
    if (!absolute.has_value()) {
        return std::unexpected(absolute.error());
    }
    const auto parent = absolute.value().parent_path();
    if (!parent.has_value()) {
        return std::unexpected(parent.error());
    }
    const auto directory = parent.value().is_directory();
    if (!directory.has_value() || !directory.value()) {
        return std::unexpected(FsError::ParentNotDirectory);
    }
    if (output_path == input_path) {
        return std::unexpected(FsError::ValidationError);
    }
    return true;
}
