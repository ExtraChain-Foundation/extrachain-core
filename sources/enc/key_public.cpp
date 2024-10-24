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

#include "enc/key_public.h"

KeyPublic::KeyPublic(const PublicKey &publicKey) {
    m_publicKey = publicKey;
}

KeyPublic::KeyPublic(const KeyPublic &keyPublic) {
    m_publicKey = keyPublic.publicKey();
}

KeyPublic::KeyPublic(const std::string &publicKey) {
    m_publicKey = ByteArray(publicKey).toArray<32>();
}

std::string KeyPublic::encrypt(const Bytes &data, const PrivateKey &senderPrivateKey) const {
    auto res = Cryptography::encryptAsymmetric(data, senderPrivateKey, m_publicKey);
    return std::string(res.begin(), res.end());
}

bool KeyPublic::verify(const Bytes &data, const Signature &signature) const {
    return Cryptography::verify(data, m_publicKey, signature);
}

bool KeyPublic::verify(const std::string &data, const Signature &signature) const {
    return Cryptography::verify(ByteArray(data).toBytes(), m_publicKey, signature);
}

const PublicKey &KeyPublic::publicKey() const {
    return m_publicKey;
}

bool KeyPublic::empty() const {
    return m_publicKey.empty();
}
