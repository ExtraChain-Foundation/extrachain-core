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

KeyPrivate::KeyPrivate()
{
    secKey = string();
    pubKey = string();
}

KeyPrivate::KeyPrivate(const KeyPrivate &keyPrivate)
{
    secKey = keyPrivate.getSecKey();
    pubKey = keyPrivate.getPubKey();
}

KeyPrivate::KeyPrivate(const QJsonObject &json)
{
    secKey = json["privateKey"].toString().toStdString();
    pubKey = json["publicKey"].toString().toStdString();
}

KeyPrivate::~KeyPrivate()
{
}

void KeyPrivate::generate()
{
    vector<unsigned char> sk(crypto_sign_SECRETKEYBYTES);
    vector<unsigned char> pk(crypto_sign_PUBLICKEYBYTES);
    crypto_sign_keypair(pk.data(), sk.data());
    secKey = Utils::byteToHexString(sk);
    secKey.erase(--secKey.end());
    pubKey = Utils::byteToHexString(pk);
    pubKey.erase(--pubKey.end());
}

QByteArray KeyPrivate::encrypt(const QByteArray &data, const string &publicKeyReceiver, const string &nonce)
{
    if (data.isEmpty() || publicKeyReceiver.empty() || nonce.empty())
        qFatal("[KeyPrivate::encrypt] msg or secret is empty. msg: %s, secret: %s, nonce: %s", data.data(),
               publicKeyReceiver.data(), nonce.data());

    string sdata = data.toStdString();
    unsigned long long enc_size = crypto_box_MACBYTES + sdata.length();
    string pkrs = Utils::hexStringToByte(publicKeyReceiver);
    vector<unsigned char> pkr(pkrs.begin(), pkrs.end());
    string sk = Utils::hexStringToByte(secKey);
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
        qFatal("[KeyPrivate::encrypt] res is empty. msg: %s, secret: %s, nonce: %s", data.data(),
               publicKeyReceiver.data(), nonce.data());
    return QByteArray::fromStdString(res);
}

QByteArray KeyPrivate::decrypt(const QByteArray &data, const string &publicKeySender, const string &nonce)
{
    if (data.isEmpty() || publicKeySender.empty() || nonce.empty())
        qFatal("[KeyPrivate::decrypt] msg or secret is empty. msg: %s, secret: %s, nonce: %s", data.data(),
               publicKeySender.data(), nonce.data());

    string sdata = Utils::hexStringToByte(data.toStdString());
    string pksr = Utils::hexStringToByte(publicKeySender);
    string sk = Utils::hexStringToByte(secKey);
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
        qFatal("[KeyPrivate::decrypt] Incorrect msg: %s", data.data());

    vector<unsigned char> skr(sk.begin(), sk.end());
    vector<unsigned char> pks(pksr.begin(), pksr.end());

    vector<unsigned char> xskr(crypto_scalarmult_curve25519_BYTES);
    crypto_sign_ed25519_sk_to_curve25519(xskr.data(), skr.data());

    vector<unsigned char> xpks(crypto_scalarmult_curve25519_BYTES);
    crypto_sign_ed25519_pk_to_curve25519(xpks.data(), pks.data());

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
        qFatal("[KeyPrivate::decrypt] res is empty. msg: %s, secret: %s, nonce: %s", data.data(),
               publicKeySender.data(), nonce.data());
    return QByteArray::fromStdString(res);
}

QByteArray KeyPrivate::encryptSelf(const QByteArray &data)
{
    string sk = Utils::hexStringToByte(secKey);
    string pnonce = sk.substr(0, crypto_box_NONCEBYTES);
    return this->encrypt(data, this->pubKey, pnonce);
}

QByteArray KeyPrivate::decryptSelf(const QByteArray &data)
{
    string sk = Utils::hexStringToByte(secKey);
    string pnonce = sk.substr(0, crypto_box_NONCEBYTES);
    return this->decrypt(data, this->pubKey, pnonce);
}

QByteArray KeyPrivate::sign(const QByteArray &data)
{
    string sks = Utils::hexStringToByte(secKey);
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
    string pks = Utils::hexStringToByte(this->pubKey);
    string signature = Utils::hexStringToByte(dsignHex.toStdString());
    vector<unsigned char> pk(pks.begin(), pks.end());
    vector<unsigned char> vmsg(data.begin(), data.end());
    vector<unsigned char> vsig(signature.begin(), signature.end());
    if (crypto_sign_verify_detached(vsig.data(), vmsg.data(), vmsg.size(), pk.data()) == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

std::string KeyPrivate::getSecKey() const
{
    return secKey;
}

std::string KeyPrivate::getPubKey() const
{
    return pubKey;
}
