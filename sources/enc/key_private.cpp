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

#include <QJsonObject>

using std::string, std::vector;

KeyPrivate::KeyPrivate() {
    generate();
}

KeyPrivate::KeyPrivate(const std::string &secret_key, const std::string &public_key) {
    m_secretKey = secret_key;
    m_publicKey = public_key;
}

KeyPrivate::KeyPrivate(const KeyPrivate &keyPrivate) {
    m_secretKey = keyPrivate.secretKey();
    m_publicKey = keyPrivate.publicKey();
}

KeyPrivate::KeyPrivate(const QJsonObject &json) {
    m_secretKey = json["privateKey"].toString().toStdString();
    m_publicKey = json["publicKey"].toString().toStdString();
}

void KeyPrivate::generate() {
    auto keys = SecretKey::createAsymmetricPair();
    m_secretKey = keys.first;
    m_publicKey = keys.second;
}

QByteArray KeyPrivate::encrypt(const QByteArray &data, const std::string &receiverPublicKey,
                               const string &nonce) {
    auto res = SecretKey::encryptAsymmetric(data.toStdString(), m_secretKey, receiverPublicKey, nonce);
    return QByteArray::fromStdString(res);
}

QByteArray KeyPrivate::decrypt(const QByteArray &data, const string &senderPublicKey, const string &nonce) {
    auto res = SecretKey::decryptAsymmetric(data.toStdString(), m_secretKey, senderPublicKey, nonce);
    return QByteArray::fromStdString(res);
}

QByteArray KeyPrivate::encryptSelf(const QByteArray &data) {
    string sk = Utils::hexStringToByte(m_secretKey);
    string pnonce = sk.substr(0, crypto_box_NONCEBYTES);
    return this->encrypt(data, this->m_publicKey, pnonce);
}

QByteArray KeyPrivate::decryptSelf(const QByteArray &data) {
    string sk = Utils::hexStringToByte(m_secretKey);
    string pnonce = sk.substr(0, crypto_box_NONCEBYTES);
    return this->decrypt(data, this->m_publicKey, pnonce);
}

QByteArray KeyPrivate::sign(const QByteArray &data) {
    string sks = Utils::hexStringToByte(m_secretKey);
    vector<unsigned char> sk(sks.begin(), sks.end());
    vector<unsigned char> vmsg(data.begin(), data.end());
    vector<unsigned char> vsig(crypto_sign_BYTES);
    crypto_sign_detached(vsig.data(), NULL, vmsg.data(), vmsg.size(), sk.data());
    string sig = Utils::byteToHexString(vsig);
    sig.erase(--sig.end());
    return QByteArray::fromStdString(sig);
}

bool KeyPrivate::verify(const QByteArray &data, const QByteArray &dsignHex) {
    string pks = Utils::hexStringToByte(this->m_publicKey);
    string signature = Utils::hexStringToByte(dsignHex.toStdString());
    vector<unsigned char> pk(pks.begin(), pks.end());
    vector<unsigned char> vmsg(data.begin(), data.end());
    vector<unsigned char> vsig(signature.begin(), signature.end());
    int res = crypto_sign_verify_detached(vsig.data(), vmsg.data(), vmsg.size(), pk.data());
    return res == 0;
}

std::string KeyPrivate::secretKey() const {
    return m_secretKey;
}

std::string KeyPrivate::publicKey() const {
    return m_publicKey;
}
