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

using std::string, std::vector;

KeyPublic::KeyPublic(const string &publicKey)
{
    pubKey = publicKey;
}

KeyPublic::KeyPublic(const QJsonObject &json)
{
    pubKey = json["publicKey"].toString().toStdString();
}

KeyPublic::KeyPublic(const KeyPublic &keyPublic)
{
    pubKey = keyPublic.getPubKey();
}

KeyPublic::~KeyPublic()
{
}

QByteArray KeyPublic::encrypt(const QByteArray &data, const string &privateKeySender)
{
    string sdata = data.toStdString();
    unsigned long long enc_size = crypto_box_MACBYTES + sdata.length();
    string pkrs = Utils::hexStringToByte(pubKey);
    vector<unsigned char> pkr(pkrs.begin(), pkrs.end());
    string sk = Utils::hexStringToByte(privateKeySender);
    vector<unsigned char> sks(sk.begin(), sk.end());

    vector<unsigned char> xsks(crypto_scalarmult_curve25519_BYTES);
    crypto_sign_ed25519_sk_to_curve25519(xsks.data(), sks.data());

    vector<unsigned char> xpkr(crypto_scalarmult_curve25519_BYTES);
    int conv_res = crypto_sign_ed25519_pk_to_curve25519(xpkr.data(), pkr.data());
    (void)conv_res; // unused
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
        res.erase(--res.end());
    }
    return QByteArray::fromStdString(res);
}

bool KeyPublic::verify(const QByteArray &data, const QByteArray &dsignHex)
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

bool KeyPublic::isEmpty()
{
    return pubKey.empty();
}

string KeyPublic::getPubKey() const
{
    return pubKey;
}
