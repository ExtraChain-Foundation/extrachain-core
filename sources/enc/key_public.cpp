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
#include "enc/enc_tools.h"
#include "utils/exc_utils.h"

KeyPublic::KeyPublic(const std::string &publicKey) {
    m_publicKey = publicKey;
}

KeyPublic::KeyPublic(const KeyPublic &keyPublic) {
    m_publicKey = keyPublic.publicKey();
}

std::string KeyPublic::encrypt(const std::string &data, const std::string &senderPrivateKey) const {
    return SecretKey::encryptAsymmetric(data, senderPrivateKey, m_publicKey);
}

bool KeyPublic::verify(const std::string &data, const std::string &signature) const {
    return SecretKey::verify(data, m_publicKey, signature);
}

const std::string &KeyPublic::publicKey() const {
    return m_publicKey;
}

bool KeyPublic::empty() const {
    return m_publicKey.empty();
}

QDebug operator<<(QDebug debug, const KeyPublic &key) {
    QDebugStateSaver saver(debug);
    debug << "KeyPublic { public: " << Utils::bytesEncode(key.publicKey().c_str()) << " }";
    return debug;
}

std::ostream &operator<<(std::ostream &os, const KeyPublic &key) {
    os << "KeyPublic { public: " << Utils::bytesEncode(key.publicKey().c_str()).toStdString() << " }";
    return os;
}
