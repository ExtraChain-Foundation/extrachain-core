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

#include <sodium.h>

#include <QJsonObject>

using std::string, std::vector;

KeyPublic::KeyPublic(const string &publicKey) {
    m_publicKey = publicKey;
}

KeyPublic::KeyPublic(const KeyPublic &keyPublic) {
    m_publicKey = keyPublic.publicKey();
}

QByteArray KeyPublic::encrypt(const QByteArray &data, const string &senderPrivateKey) const {
    auto res = SecretKey::encryptAsymmetric(data.toStdString(), senderPrivateKey, m_publicKey);
    return QByteArray::fromStdString(res);
}

bool KeyPublic::verify(const QByteArray &data, const QByteArray &dsignHex) const {
    string pks = Utils::hexStringToByte(this->m_publicKey);
    string signature = Utils::hexStringToByte(dsignHex.toStdString());
    vector<unsigned char> pk(pks.begin(), pks.end());
    vector<unsigned char> vmsg(data.begin(), data.end());
    vector<unsigned char> vsig(signature.begin(), signature.end());
    int res = crypto_sign_verify_detached(vsig.data(), vmsg.data(), vmsg.size(), pk.data());
    return res == 0;
}

const std::string &KeyPublic::publicKey() const {
    return m_publicKey;
}

bool KeyPublic::empty() const {
    return m_publicKey.empty();
}
