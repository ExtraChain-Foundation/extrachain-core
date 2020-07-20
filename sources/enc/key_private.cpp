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
    secKey = string(sk.begin(), sk.end());
    pubKey = string(pk.begin(), pk.end());
}

QByteArray KeyPrivate::encrypt(const QByteArray &data, const string &publicKeyReceiver)
{
    string sdata = data.toStdString();
    unsigned long long enc_size = crypto_box_MACBYTES + sdata.length();

    vector<unsigned char> pkr(publicKeyReceiver.begin(), publicKeyReceiver.end());
    vector<unsigned char> sks(this->secKey.begin(), this->secKey.end());

    vector<unsigned char> xsks(crypto_scalarmult_curve25519_BYTES);
    crypto_sign_ed25519_sk_to_curve25519(xsks.data(), sks.data());

    vector<unsigned char> xpkr(crypto_scalarmult_curve25519_BYTES);
    int conv_res = crypto_sign_ed25519_pk_to_curve25519(xpkr.data(), pkr.data());
    //    if (conv_res)
    vector<unsigned char> enc_msg(enc_size);
    vector<unsigned char> dec_msg(sdata.begin(), sdata.end());
    vector<unsigned char> nonce;
    nonce.resize(crypto_box_NONCEBYTES);
    randombytes_buf(nonce.data(), nonce.size());
    int r = crypto_box_easy(enc_msg.data(), dec_msg.data(), dec_msg.size(), nonce.data(), xpkr.data(),
                            xsks.data());
    string res;
    if (r == 0)
    {
        enc_msg.insert(enc_msg.begin(), nonce.begin(), nonce.end());
        res = Utils::byteToHexString(enc_msg);
    }
    return QByteArray::fromStdString(res);
}

QByteArray KeyPrivate::decrypt(const QByteArray &data, const string &publicKeySender)
{
    string sdata = Utils::hexStringToByte(data.toStdString());
    string s_nonce = sdata.substr(0, crypto_box_NONCEBYTES);
    sdata.erase(0, crypto_box_NONCEBYTES);
    vector<unsigned char> nonce(s_nonce.begin(), s_nonce.end());

    vector<unsigned char> skr(this->secKey.begin(), this->secKey.end());
    vector<unsigned char> pks(publicKeySender.begin(), publicKeySender.end());

    vector<unsigned char> xskr(crypto_scalarmult_curve25519_BYTES);
    crypto_sign_ed25519_sk_to_curve25519(xskr.data(), skr.data());

    vector<unsigned char> xpks(crypto_scalarmult_curve25519_BYTES);
    crypto_sign_ed25519_pk_to_curve25519(xpks.data(), pks.data());

    vector<unsigned char> enc_msg(sdata.begin(), sdata.end());
    vector<unsigned char> dec_msg(enc_msg.size() - crypto_box_MACBYTES);

    int r = crypto_box_open_easy(dec_msg.data(), enc_msg.data(), enc_msg.size(), nonce.data(), xpks.data(),
                                 xskr.data());
    string res;
    if (r == 0)
    {
        res = string(dec_msg.begin(), dec_msg.end());
    }
    return QByteArray::fromStdString(res);
}

QByteArray KeyPrivate::encryptSelf(const QByteArray &data)
{
    return this->encrypt(data, this->pubKey);
}

QByteArray KeyPrivate::decryptSelf(const QByteArray &data)
{
    return this->decrypt(data, this->pubKey);
}

QByteArray KeyPrivate::sign(const QByteArray &data)
{
    vector<unsigned char> sk(secKey.begin(), secKey.end());
    vector<unsigned char> vmsg(data.begin(), data.end());
    vector<unsigned char> vsig(crypto_sign_BYTES);
    crypto_sign_detached(vsig.data(), NULL, vmsg.data(), vmsg.size(), sk.data());
    string sig = Utils::byteToHexString(vsig);
    return QByteArray::fromStdString(sig);
}

bool KeyPrivate::verify(const QByteArray &data, const QByteArray &dsignHex)
{
    string signature = Utils::hexStringToByte(dsignHex.toStdString());
    vector<unsigned char> pk(this->pubKey.begin(), this->pubKey.end());
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

string KeyPrivate::getSecHexKey() const
{
    return Utils::byteToHexString(secKey);
}

string KeyPrivate::getPubHexKey() const
{
    return Utils::byteToHexString(pubKey);
}
