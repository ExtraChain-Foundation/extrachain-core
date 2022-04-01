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
#include <fstream>
#include <sodium.h>

using std::string, std::vector;

KeyPrivate::KeyPrivate(const std::string &secret_key, const std::string &public_key) {
    m_secretKey = secret_key;
    m_publicKey = public_key;
}

KeyPrivate::KeyPrivate(const KeyPrivate &keyPrivate) {
    m_secretKey = keyPrivate.secretKey();
    m_publicKey = keyPrivate.publicKey();
}

void KeyPrivate::generate() {
    auto keys = SecretKey::createAsymmetricPair();
    m_secretKey = keys.first;
    m_publicKey = keys.second;
}

std::string KeyPrivate::encrypt(const std::string &data, const std::string &receiverPublicKey,
                                const string &nonce) const {
    auto res = SecretKey::encryptAsymmetric(data, m_secretKey, receiverPublicKey, nonce);
    return res;
}

std::string KeyPrivate::decrypt(const std::string &data, const string &senderPublicKey,
                                const string &nonce) const {
    auto res = SecretKey::decryptAsymmetric(data, m_secretKey, senderPublicKey, nonce);
    return res;
}

std::string KeyPrivate::encryptSelf(const std::string &data) const {
    string sk = Utils::hexStringToByte(m_secretKey);
    string pnonce = sk.substr(0, crypto_box_NONCEBYTES);
    return this->encrypt(data, this->m_publicKey, pnonce);
}

std::string KeyPrivate::decryptSelf(const std::string &data) const {
    string sk = Utils::hexStringToByte(m_secretKey);
    string pnonce = sk.substr(0, crypto_box_NONCEBYTES);
    return this->decrypt(data, this->m_publicKey, pnonce);
}

void KeyPrivate::encryptFile(const std::filesystem::path file, const std::filesystem::path resultFile) const {
    std::ifstream sfile(file.string(), std::ios::binary);
    std::ofstream efile(resultFile.string(), std::ios::binary | std::ios::app);
    if (sfile && efile) {
        std::string buf;
        while (sfile.readsome(buf.data(), DFS::Basic::encSectionSize)) {
            std::string wstring = encryptSelf(buf);
            wstring = Tools::typeToStdStringBytes<int>(wstring.size()) + wstring;
            efile << wstring;
        }
        return;
    }
    qDebug() << "file encryption error";
}

void KeyPrivate::decryptFile(const std::filesystem::path file, const std::filesystem::path resultFile) const {
    std::ifstream efile(file.string(), std::ios::binary);
    std::ofstream sfile(resultFile.string(), std::ios::binary | std::ios::app);
    if (efile && sfile) {
        std::string buf;
        std::string sizebuf;
        int size;
        while (efile.readsome(sizebuf.data(), sizeof(int))) {
            size = Tools::stdStringBytesToType<int>(sizebuf);
            efile.readsome(buf.data(), size);
            std::string wstring = decryptSelf(buf);
            sfile << wstring;
        }
        return;
    }
    qDebug() << "file decryption error";
}

std::string KeyPrivate::sign(const std::string &data) const {
    string sks = Utils::hexStringToByte(m_secretKey);
    vector<unsigned char> sk(sks.begin(), sks.end());
    vector<unsigned char> vmsg(data.begin(), data.end());
    vector<unsigned char> vsig(crypto_sign_BYTES);
    crypto_sign_detached(vsig.data(), NULL, vmsg.data(), vmsg.size(), sk.data());
    string sig = Utils::byteToHexString(vsig);
    sig.erase(--sig.end());
    return sig;
}

bool KeyPrivate::verify(const std::string &data, const std::string &dsignHex) const {
    string pks = Utils::hexStringToByte(this->m_publicKey);
    string signature = Utils::hexStringToByte(dsignHex);
    vector<unsigned char> pk(pks.begin(), pks.end());
    vector<unsigned char> vmsg(data.begin(), data.end());
    vector<unsigned char> vsig(signature.begin(), signature.end());
    int res = crypto_sign_verify_detached(vsig.data(), vmsg.data(), vmsg.size(), pk.data());
    return res == 0;
}

const std::string &KeyPrivate::secretKey() const {
    return m_secretKey;
}

const std::string &KeyPrivate::publicKey() const {
    return m_publicKey;
}

bool KeyPrivate::empty() const {
    return m_secretKey.empty() && m_publicKey.empty();
}
