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

#include "enc/key_private.h"
#include "enc/enc_tools.h"
#include "utils/dfs_utils.h"
#include "utils/exc_utils.h"

#include <sodium.h>

#include <fstream>

KeyPrivate::KeyPrivate(const PrivateKey &secret_key, const PublicKey &public_key) {
    m_secretKey = secret_key;
    m_publicKey = public_key;
}

KeyPrivate::KeyPrivate(const std::string &secret_key, const std::string &public_key) {
    m_secretKey = ByteArray(secret_key).toArray<64>();
    m_publicKey = ByteArray(public_key).toArray<32>();
}

KeyPrivate::KeyPrivate(const KeyPrivate &keyPrivate) {
    m_secretKey = keyPrivate.secretKey();
    m_publicKey = keyPrivate.publicKey();
}

void KeyPrivate::generate() {
    auto [secretKey, publicKey] = Cryptography::createAsymmetricPair();
    m_secretKey                 = secretKey;
    m_publicKey                 = publicKey;
}

Bytes KeyPrivate::encrypt(const Bytes &data, const PublicKey &receiverPublicKey, const Nonce &nonce) const {
    return Cryptography::encryptAsymmetric(data, m_secretKey, receiverPublicKey, nonce);
}

Bytes KeyPrivate::decrypt(const Bytes &data, const PublicKey &senderPublicKey, const Nonce &nonce) const {
    return Cryptography::decryptAsymmetric(data, m_secretKey, senderPublicKey, nonce);
}

Bytes KeyPrivate::encryptSelf(const Bytes &data) const {
    Nonce nonce;
    std::copy_n(
        m_secretKey.begin(),
        std::min(size_t(crypto_box_NONCEBYTES), m_secretKey.size()),
        nonce.begin());

    return this->encrypt(data, this->m_publicKey, nonce);
}

Bytes KeyPrivate::decryptSelf(const Bytes &data) const {
    Nonce nonce;
    std::copy_n(
        m_secretKey.begin(),
        std::min(size_t(crypto_box_NONCEBYTES), m_secretKey.size()),
        nonce.begin());

    return this->decrypt(data, this->m_publicKey, nonce);
}

void KeyPrivate::encryptFile(const std::filesystem::path &input, const std::filesystem::path &output) const {
    std::ifstream in(input, std::ios::binary);
    std::ofstream out(output, std::ios::binary);

    if (!in || !out) {
        qFatal("[encryptFile] Cannot open files");
    }

    Bytes buffer(DFSB::encSectionSize);
    while (in.read(reinterpret_cast<char *>(buffer.data()), buffer.size())) {
        Bytes    encrypted = encryptSelf({ buffer.begin(), buffer.begin() + in.gcount() });
        uint32_t size      = encrypted.size();

        out.write(reinterpret_cast<char *>(&size), sizeof(size));
        out.write(reinterpret_cast<char *>(encrypted.data()), size);
    }
}

void KeyPrivate::decryptFile(const std::filesystem::path &input, const std::filesystem::path &output) const {
    std::ifstream in(input, std::ios::binary);
    std::ofstream out(output, std::ios::binary);

    if (!in || !out) {
        qFatal("[decryptFile] Cannot open files");
    }

    uint32_t size;
    while (in.read(reinterpret_cast<char *>(&size), sizeof(size))) {
        Bytes encrypted(size);

        if (in.read(reinterpret_cast<char *>(encrypted.data()), size)) {
            Bytes decrypted = decryptSelf(encrypted);
            out.write(reinterpret_cast<char *>(decrypted.data()), decrypted.size());
        }
    }
}

Signature KeyPrivate::sign(const Bytes &data) const {
    return Cryptography::sign(data, m_secretKey);
}

bool KeyPrivate::verify(const Bytes &data, const Signature &signature) const {
    return Cryptography::verify(data, m_publicKey, signature);
}

Signature KeyPrivate::sign(const std::string &data) const {
    return Cryptography::sign(ByteArray(data).toBytes(), m_secretKey);
}

bool KeyPrivate::verify(const std::string &data, const Signature &signature) const {
    return Cryptography::verify(ByteArray(data).toBytes(), m_publicKey, signature);
}

const PrivateKey &KeyPrivate::secretKey() const {
    return m_secretKey;
}

const PublicKey &KeyPrivate::publicKey() const {
    return m_publicKey;
}

bool KeyPrivate::empty() const {
    return Utils::isAllEmpty(m_secretKey) || Utils::isAllEmpty(m_publicKey);
}
