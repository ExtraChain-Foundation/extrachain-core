#ifndef ENC_TOOLS_H
#define ENC_TOOLS_H

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

EXTRACHAIN_EXPORT KeyPass getKeyFromPass(const std::string &pass, const Salt &salt = {});

PrivateKey deriveKey(const KeyPass &key);

EXTRACHAIN_EXPORT Signature sign(const Bytes &data, const PrivateKey &secret_key);
EXTRACHAIN_EXPORT bool verify(const Bytes &data, const PublicKey &public_key, const Signature &signature);

EXTRACHAIN_EXPORT Bytes encrypt(const Bytes &data, const PrivateKey &secret_key);
EXTRACHAIN_EXPORT Bytes decrypt(const Bytes &data, const PrivateKey &secret_key);
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
                               const Nonce      &nonce = {});
EXTRACHAIN_EXPORT Bytes decryptAsymmetric(
    const Bytes      &data,
    const PrivateKey &secret_key,
    const PublicKey  &public_key,
    const Nonce      &nonce = {});
}

#endif // ENC_TOOLS_H
