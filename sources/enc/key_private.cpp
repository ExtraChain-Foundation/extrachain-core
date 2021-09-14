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

KeyPrivate::KeyPrivate()
{
    generate();
}

KeyPrivate::KeyPrivate(const KeyPrivate &keyPrivate)
{
    m_secretKey = keyPrivate.secretKey();
    m_publicKey = keyPrivate.publicKey();
}

KeyPrivate::KeyPrivate(const QJsonObject &json)
{
    m_secretKey = json["privateKey"].toString().toStdString();
    m_publicKey = json["publicKey"].toString().toStdString();
}

KeyPrivate::~KeyPrivate()
{
}

void KeyPrivate::generate()
{
    vector<unsigned char> sk(crypto_sign_SECRETKEYBYTES);
    vector<unsigned char> pk(crypto_sign_PUBLICKEYBYTES);
    crypto_sign_keypair(pk.data(), sk.data());
    m_secretKey = Utils::byteToHexString(sk);
    m_secretKey.erase(--m_secretKey.end());
    m_publicKey = Utils::byteToHexString(pk);
    m_publicKey.erase(--m_publicKey.end());
}

QByteArray KeyPrivate::encrypt(const QByteArray &data, const string &receiverPublicKey, const string &nonce)
{
    if (data.isEmpty() || receiverPublicKey.empty())
        qFatal("[KeyPrivate::encrypt] msg or secret is empty. msg: %s, secret: %s", data.data(),
               receiverPublicKey.data());

    string sdata = data.toStdString();
    unsigned long long enc_size = crypto_box_MACBYTES + sdata.length();
    string pkrs = Utils::hexStringToByte(receiverPublicKey);
    vector<unsigned char> pkr(pkrs.begin(), pkrs.end());
    string sk = Utils::hexStringToByte(m_secretKey);
    vector<unsigned char> sks(sk.begin(), sk.end());

    vector<unsigned char> xsks(crypto_scalarmult_curve25519_BYTES);
    crypto_sign_ed25519_sk_to_curve25519(xsks.data(), sks.data());

    vector<unsigned char> xpkr(crypto_scalarmult_curve25519_BYTES);
    int conv_res = crypto_sign_ed25519_pk_to_curve25519(xpkr.data(), pkr.data());
    (void)conv_res;

    vector<unsigned char> enc_msg(enc_size);
    vector<unsigned char> dec_msg(sdata.begin(), sdata.end());
    vector<unsigned char> vnonce;
    vnonce.resize(crypto_box_NONCEBYTES);
    if (nonce.size() == crypto_box_NONCEBYTES)
    {
        vnonce = vector<unsigned char>(nonce.begin(), nonce.end());
    }
    else
    {
        randombytes_buf(vnonce.data(), vnonce.size());
    }
    int r = crypto_box_easy(enc_msg.data(), dec_msg.data(), dec_msg.size(), vnonce.data(), xpkr.data(),
                            xsks.data());
    string res;
    if (r == 0)
    {
        if (nonce.size() != crypto_box_NONCEBYTES)
        {
            enc_msg.insert(enc_msg.begin(), vnonce.begin(), vnonce.end());
        }
        res = Utils::byteToHexString(enc_msg);
        res.erase(--res.end());
    }
    if (res.empty())
        qDebug() << "[KeyPrivate::encrypt] res is empty. msg:" << data.data()
                 << "| secret:" << receiverPublicKey.data() << "| nonce:" << nonce.data();
    return QByteArray::fromStdString(res);
}

QByteArray KeyPrivate::decrypt(const QByteArray &data, const string &senderPublicKey, const string &nonce)
{
    if (data.isEmpty() || senderPublicKey.empty())
        qFatal("[KeyPrivate::decrypt] msg or secret is empty. msg: %s, secret: %s", data.data(),
               senderPublicKey.data());

    string sdata = Utils::hexStringToByte(data.toStdString());
    string pksr = Utils::hexStringToByte(senderPublicKey);
    string sk = Utils::hexStringToByte(m_secretKey);
    vector<unsigned char> vnonce;
    if (nonce.size() == crypto_box_NONCEBYTES)
    {
        vnonce = vector<unsigned char>(nonce.begin(), nonce.end());
    }
    else
    {
        string s_nonce = sdata.substr(0, crypto_box_NONCEBYTES);
        sdata.erase(0, crypto_box_NONCEBYTES);
        vnonce = vector<unsigned char>(s_nonce.begin(), s_nonce.end());
    }

    if (sdata.size() < crypto_secretbox_MACBYTES)
    {
        qCritical() << "Critical: [KeyPrivate::decrypt] Incorrect msg" << sdata.size()
                    << crypto_secretbox_MACBYTES;
        return "";
        qFatal("[KeyPrivate::decrypt] Incorrect msg");
    }

    vector<unsigned char> skr(sk.begin(), sk.end());
    vector<unsigned char> pks(pksr.begin(), pksr.end());

    vector<unsigned char> xskr(crypto_scalarmult_curve25519_BYTES);
    crypto_sign_ed25519_sk_to_curve25519(xskr.data(), skr.data());

    vector<unsigned char> xpks(crypto_scalarmult_curve25519_BYTES);
    int res_ed_to_curve = crypto_sign_ed25519_pk_to_curve25519(xpks.data(), pks.data());
    (void)res_ed_to_curve; // unused

    vector<unsigned char> enc_msg(sdata.begin(), sdata.end());
    vector<unsigned char> dec_msg(enc_msg.size() - crypto_box_MACBYTES);

    int r = crypto_box_open_easy(dec_msg.data(), enc_msg.data(), enc_msg.size(), vnonce.data(), xpks.data(),
                                 xskr.data());
    string res;
    if (r == 0)
    {
        res = string(dec_msg.begin(), dec_msg.end());
    }
    if (res.empty())
        qDebug() << "[KeyPrivate::encrypt] res is empty." /*msg:" << data.data()*/
                 << "| secret:" << senderPublicKey.data() << "| nonce:" << nonce.data();
    return QByteArray::fromStdString(res);
}

QByteArray KeyPrivate::encryptSelf(const QByteArray &data)
{
    string sk = Utils::hexStringToByte(m_secretKey);
    string pnonce = sk.substr(0, crypto_box_NONCEBYTES);
    return this->encrypt(data, this->m_publicKey, pnonce);
}

QByteArray KeyPrivate::decryptSelf(const QByteArray &data)
{
    string sk = Utils::hexStringToByte(m_secretKey);
    string pnonce = sk.substr(0, crypto_box_NONCEBYTES);
    return this->decrypt(data, this->m_publicKey, pnonce);
}

QByteArray KeyPrivate::sign(const QByteArray &data)
{
    string sks = Utils::hexStringToByte(m_secretKey);
    vector<unsigned char> sk(sks.begin(), sks.end());
    vector<unsigned char> vmsg(data.begin(), data.end());
    vector<unsigned char> vsig(crypto_sign_BYTES);
    crypto_sign_detached(vsig.data(), NULL, vmsg.data(), vmsg.size(), sk.data());
    string sig = Utils::byteToHexString(vsig);
    sig.erase(--sig.end());
    return QByteArray::fromStdString(sig);
}

bool KeyPrivate::verify(const QByteArray &data, const QByteArray &dsignHex)
{
    string pks = Utils::hexStringToByte(this->m_publicKey);
    string signature = Utils::hexStringToByte(dsignHex.toStdString());
    vector<unsigned char> pk(pks.begin(), pks.end());
    vector<unsigned char> vmsg(data.begin(), data.end());
    vector<unsigned char> vsig(signature.begin(), signature.end());
    int res = crypto_sign_verify_detached(vsig.data(), vmsg.data(), vmsg.size(), pk.data());
    return res == 0;
}

std::string KeyPrivate::secretKey() const
{
    return m_secretKey;
}

std::string KeyPrivate::publicKey() const
{
    return m_publicKey;
}
