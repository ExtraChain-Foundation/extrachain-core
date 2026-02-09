/*
 * ExtraChain Core — C FFI Standalone Cryptography
 */

#include "exc_internal.h"

#include "chain/actor.h"
#include "encryption/key_private.h"
#include "encryption/key_public.h"
#include "encryption/encryption_tools.h"

using namespace exc_ffi;

/* ── Hex conversion helpers ──────────────────────────────────────── */

namespace {

template <size_t N>
std::string array_to_hex(const std::array<uint8_t, N>& arr) {
    std::string hex;
    hex.reserve(N * 2);
    static const char digits[] = "0123456789abcdef";
    for (auto b : arr) {
        hex.push_back(digits[b >> 4]);
        hex.push_back(digits[b & 0x0f]);
    }
    return hex;
}

template <size_t N>
bool hex_to_array(const char* hex, std::array<uint8_t, N>& out) {
    if (!hex) return false;
    size_t len = std::strlen(hex);
    if (len != N * 2) return false;

    for (size_t i = 0; i < N; ++i) {
        auto hi = hex[i * 2];
        auto lo = hex[i * 2 + 1];

        auto hexval = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

        int h = hexval(hi);
        int l = hexval(lo);
        if (h < 0 || l < 0) return false;
        out[i] = static_cast<uint8_t>((h << 4) | l);
    }
    return true;
}

std::string bytes_to_hex(const std::vector<uint8_t>& data) {
    std::string hex;
    hex.reserve(data.size() * 2);
    static const char digits[] = "0123456789abcdef";
    for (auto b : data) {
        hex.push_back(digits[b >> 4]);
        hex.push_back(digits[b & 0x0f]);
    }
    return hex;
}

} // anonymous namespace

extern "C" {

EXC_API ExcError exc_crypto_generate_keypair(char** out_actor_id,
                                             char** out_secret_key,
                                             char** out_public_key) {
    EXC_CHECK_NULL(out_actor_id);
    EXC_CHECK_NULL(out_secret_key);
    EXC_CHECK_NULL(out_public_key);

    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);

    *out_actor_id = exc_strdup(actor.id().to_string());
    *out_secret_key = exc_strdup(array_to_hex(actor.key().secret_key()));
    *out_public_key = exc_strdup(array_to_hex(actor.key().public_key()));

    return EXC_OK;
}

EXC_API ExcError exc_crypto_sign(const uint8_t* data, size_t data_len,
                                 const char* secret_key_hex, const char* public_key_hex,
                                 char** out_signature_hex) {
    EXC_CHECK_NULL(data);
    EXC_CHECK_NULL(secret_key_hex);
    EXC_CHECK_NULL(public_key_hex);
    EXC_CHECK_NULL(out_signature_hex);

    PrivateKey sk;
    PublicKey pk;
    if (!hex_to_array(secret_key_hex, sk)) return EXC_ERR_CRYPTO_EMPTY_KEY;
    if (!hex_to_array(public_key_hex, pk)) return EXC_ERR_CRYPTO_EMPTY_KEY;

    KeyPrivate key(sk, pk);
    Bytes input(data, data + data_len);
    auto result = key.sign(input);
    if (!result.has_value()) {
        return EXC_ERR_CRYPTO_ENCRYPT_FAILED;
    }

    *out_signature_hex = exc_strdup(array_to_hex(result.value()));
    return EXC_OK;
}

EXC_API ExcError exc_crypto_verify(const uint8_t* data, size_t data_len,
                                   const char* signature_hex, const char* public_key_hex,
                                   bool* out_valid) {
    EXC_CHECK_NULL(data);
    EXC_CHECK_NULL(signature_hex);
    EXC_CHECK_NULL(public_key_hex);
    EXC_CHECK_NULL(out_valid);

    *out_valid = false;

    PublicKey pk;
    Signature sig;
    if (!hex_to_array(public_key_hex, pk)) return EXC_ERR_CRYPTO_EMPTY_KEY;
    if (!hex_to_array(signature_hex, sig)) return EXC_ERR_CRYPTO_EMPTY_SIGN;

    KeyPublic key(pk);
    Bytes input(data, data + data_len);
    auto result = key.verify(input, sig);
    if (!result.has_value()) {
        return EXC_ERR_CRYPTO_AUTH_FAILED;
    }
    *out_valid = result.value();
    return EXC_OK;
}

EXC_API ExcError exc_crypto_encrypt(const uint8_t* data, size_t data_len,
                                    const char* sender_secret_hex,
                                    const char* receiver_public_hex,
                                    ExcBytes* out_encrypted) {
    EXC_CHECK_NULL(data);
    EXC_CHECK_NULL(sender_secret_hex);
    EXC_CHECK_NULL(receiver_public_hex);
    EXC_CHECK_NULL(out_encrypted);

    PrivateKey sk;
    PublicKey rpk;
    if (!hex_to_array(sender_secret_hex, sk)) return EXC_ERR_CRYPTO_EMPTY_KEY;
    if (!hex_to_array(receiver_public_hex, rpk)) return EXC_ERR_CRYPTO_EMPTY_KEY;

    /* Need the sender's public key too — derive from secret key */
    PublicKey spk = Cryptography::get_public_from_private(sk);
    KeyPrivate key(sk, spk);

    Bytes input(data, data + data_len);
    auto result = key.encrypt(input, rpk);
    if (!result.has_value()) {
        switch (result.error()) {
        case Cryptography::CryptoError::EmptyData:         return EXC_ERR_CRYPTO_EMPTY_DATA;
        case Cryptography::CryptoError::EmptyKey:          return EXC_ERR_CRYPTO_EMPTY_KEY;
        case Cryptography::CryptoError::EncryptionFailed:  return EXC_ERR_CRYPTO_ENCRYPT_FAILED;
        case Cryptography::CryptoError::KeyConversionFailed: return EXC_ERR_CRYPTO_KEY_CONVERSION;
        default:                                           return EXC_ERR_CRYPTO_ENCRYPT_FAILED;
        }
    }

    auto& encrypted = result.value();
    out_encrypted->size = encrypted.size();
    out_encrypted->data = static_cast<uint8_t*>(std::malloc(encrypted.size()));
    std::memcpy(out_encrypted->data, encrypted.data(), encrypted.size());
    return EXC_OK;
}

EXC_API ExcError exc_crypto_decrypt(const uint8_t* data, size_t data_len,
                                    const char* receiver_secret_hex,
                                    const char* sender_public_hex,
                                    ExcBytes* out_decrypted) {
    EXC_CHECK_NULL(data);
    EXC_CHECK_NULL(receiver_secret_hex);
    EXC_CHECK_NULL(sender_public_hex);
    EXC_CHECK_NULL(out_decrypted);

    PrivateKey sk;
    PublicKey spk;
    if (!hex_to_array(receiver_secret_hex, sk)) return EXC_ERR_CRYPTO_EMPTY_KEY;
    if (!hex_to_array(sender_public_hex, spk)) return EXC_ERR_CRYPTO_EMPTY_KEY;

    PublicKey rpk = Cryptography::get_public_from_private(sk);
    KeyPrivate key(sk, rpk);

    Bytes input(data, data + data_len);
    auto result = key.decrypt(input, spk);
    if (!result.has_value()) {
        switch (result.error()) {
        case Cryptography::CryptoError::EmptyData:          return EXC_ERR_CRYPTO_EMPTY_DATA;
        case Cryptography::CryptoError::DecryptionFailed:   return EXC_ERR_CRYPTO_DECRYPT_FAILED;
        case Cryptography::CryptoError::AuthenticationFailed: return EXC_ERR_CRYPTO_AUTH_FAILED;
        case Cryptography::CryptoError::DataTooShort:       return EXC_ERR_CRYPTO_DATA_TOO_SHORT;
        default:                                            return EXC_ERR_CRYPTO_DECRYPT_FAILED;
        }
    }

    auto& decrypted = result.value();
    out_decrypted->size = decrypted.size();
    out_decrypted->data = static_cast<uint8_t*>(std::malloc(decrypted.size()));
    std::memcpy(out_decrypted->data, decrypted.data(), decrypted.size());
    return EXC_OK;
}

EXC_API ExcError exc_crypto_encrypt_self(const uint8_t* data, size_t data_len,
                                         const char* secret_key_hex,
                                         const char* public_key_hex,
                                         ExcBytes* out_encrypted) {
    EXC_CHECK_NULL(data);
    EXC_CHECK_NULL(secret_key_hex);
    EXC_CHECK_NULL(public_key_hex);
    EXC_CHECK_NULL(out_encrypted);

    PrivateKey sk;
    PublicKey pk;
    if (!hex_to_array(secret_key_hex, sk)) return EXC_ERR_CRYPTO_EMPTY_KEY;
    if (!hex_to_array(public_key_hex, pk)) return EXC_ERR_CRYPTO_EMPTY_KEY;

    KeyPrivate key(sk, pk);
    Bytes input(data, data + data_len);
    auto result = key.encrypt_self(input);
    if (!result.has_value()) return EXC_ERR_CRYPTO_ENCRYPT_FAILED;

    auto& encrypted = result.value();
    out_encrypted->size = encrypted.size();
    out_encrypted->data = static_cast<uint8_t*>(std::malloc(encrypted.size()));
    std::memcpy(out_encrypted->data, encrypted.data(), encrypted.size());
    return EXC_OK;
}

EXC_API ExcError exc_crypto_decrypt_self(const uint8_t* data, size_t data_len,
                                         const char* secret_key_hex,
                                         const char* public_key_hex,
                                         ExcBytes* out_decrypted) {
    EXC_CHECK_NULL(data);
    EXC_CHECK_NULL(secret_key_hex);
    EXC_CHECK_NULL(public_key_hex);
    EXC_CHECK_NULL(out_decrypted);

    PrivateKey sk;
    PublicKey pk;
    if (!hex_to_array(secret_key_hex, sk)) return EXC_ERR_CRYPTO_EMPTY_KEY;
    if (!hex_to_array(public_key_hex, pk)) return EXC_ERR_CRYPTO_EMPTY_KEY;

    KeyPrivate key(sk, pk);
    Bytes input(data, data + data_len);
    auto result = key.decrypt_self(input);
    if (!result.has_value()) return EXC_ERR_CRYPTO_DECRYPT_FAILED;

    auto& decrypted = result.value();
    out_decrypted->size = decrypted.size();
    out_decrypted->data = static_cast<uint8_t*>(std::malloc(decrypted.size()));
    std::memcpy(out_decrypted->data, decrypted.data(), decrypted.size());
    return EXC_OK;
}

} // extern "C"
