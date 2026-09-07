#include "encryption/box_session.h"

namespace Cryptography {
    std::expected<std::unique_ptr<BoxSession>, CryptoError> BoxSession::create(const PrivateKey& local_key,
                                                                               const PublicKey&  peer_key) {
        auto          session = std::unique_ptr<BoxSession>(new BoxSession);
        Curve25519Key public_curve {}, private_curve {};
        if (crypto_sign_ed25519_pk_to_curve25519(public_curve.data(), peer_key.data()) != 0) {
            return std::unexpected(CryptoError::KeyConversionFailed);
        }
        if (crypto_sign_ed25519_sk_to_curve25519(private_curve.data(), local_key.data()) != 0) {
            sodium_memzero(private_curve.data(), private_curve.size());
            return std::unexpected(CryptoError::KeyConversionFailed);
        }
        const auto result = crypto_box_beforenm(session->key_.data(), public_curve.data(), private_curve.data());
        sodium_memzero(private_curve.data(), private_curve.size());
        if (result != 0) {
            return std::unexpected(CryptoError::EncryptionFailed);
        }
        return session;
    }

    BoxSession::~BoxSession() {
        sodium_memzero(key_.data(), key_.size());
    }

    CryptoResult BoxSession::encrypt(std::span<const std::uint8_t> data) const {
        if (data.empty()) {
            return std::unexpected(CryptoError::EmptyData);
        }
        if (data.size() > crypto_box_MESSAGEBYTES_MAX
            || data.size() > std::numeric_limits<std::size_t>::max() - MIN_ENCRYPTED_SIZE_ASYMMETRIC) {
            return std::unexpected(CryptoError::DataTooLarge);
        }
        Bytes encrypted(data.size() + MIN_ENCRYPTED_SIZE_ASYMMETRIC);
        randombytes_buf(encrypted.data(), crypto_box_NONCEBYTES);
        if (crypto_box_easy_afternm(encrypted.data() + crypto_box_NONCEBYTES,
                                    data.data(),
                                    data.size(),
                                    encrypted.data(),
                                    key_.data())
            != 0) {
            return std::unexpected(CryptoError::EncryptionFailed);
        }
        return encrypted;
    }

    CryptoResult BoxSession::decrypt(std::span<const std::uint8_t> data) const {
        if (data.empty()) {
            return std::unexpected(CryptoError::EmptyData);
        }
        if (data.size() < MIN_ENCRYPTED_SIZE_ASYMMETRIC) {
            return std::unexpected(CryptoError::DataTooShort);
        }
        Bytes decrypted(data.size() - MIN_ENCRYPTED_SIZE_ASYMMETRIC);
        if (crypto_box_open_easy_afternm(decrypted.data(),
                                         data.data() + crypto_box_NONCEBYTES,
                                         data.size() - crypto_box_NONCEBYTES,
                                         data.data(),
                                         key_.data())
            != 0) {
            return std::unexpected(CryptoError::DecryptionFailed);
        }
        return decrypted;
    }
} // namespace Cryptography
