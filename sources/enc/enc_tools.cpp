#include "enc/enc_tools.h"

string SecretKey::keygen()
{
    vector<unsigned char> sk(crypto_secretbox_KEYBYTES);
    crypto_secretbox_keygen(sk.data());
    string skey = Utils::byteToHexString(sk);
    return skey;
}

string SecretKey::getKeyFromPass(string pass, string salt)
{
    vector<unsigned char> vsalt(crypto_pwhash_SALTBYTES);
    if (salt.empty() || salt.size() < crypto_pwhash_SALTBYTES)
    {
        std::fill(vsalt.begin(), vsalt.end(), '0');
    }
    else
    {
        vsalt = vector<unsigned char>(salt.begin(), salt.end());
    }
    vector<unsigned char> key(crypto_box_SEEDBYTES);
    int rst1 = crypto_pwhash(key.data(), key.size(), pass.data(), pass.size(), vsalt.data(),
                             crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                             crypto_pwhash_ALG_DEFAULT);
    string skey = Utils::byteToHexString(key);
    return skey;
}

string SecretKey::encrypt(string msg, string &secret_key)
{
    unsigned long long enc_size = crypto_secretbox_MACBYTES + msg.length();
    string sks = Utils::hexStringToByte(secret_key);
    vector<unsigned char> sk(sks.begin(), sks.end());

    vector<unsigned char> enc_msg(enc_size);
    vector<unsigned char> dec_msg(msg.begin(), msg.end());
    vector<unsigned char> nonce;
    nonce.resize(crypto_secretbox_NONCEBYTES);
    randombytes_buf(nonce.data(), nonce.size());
    int r = crypto_secretbox_easy(enc_msg.data(), dec_msg.data(), dec_msg.size(), nonce.data(), sk.data());
    string res;
    if (r == 0)
    {
        enc_msg.insert(enc_msg.begin(), nonce.begin(), nonce.end());
        res = Utils::byteToHexString(enc_msg);
    }
    return res;
}

string SecretKey::decrypt(string msg, string &secret_key)
{
    string sdata = Utils::hexStringToByte(msg);
    string s_nonce = sdata.substr(0, crypto_secretbox_NONCEBYTES);
    sdata.erase(0, crypto_secretbox_NONCEBYTES);
    vector<unsigned char> nonce(s_nonce.begin(), s_nonce.end());
    string sks = Utils::hexStringToByte(secret_key);
    vector<unsigned char> sk(sks.begin(), sks.end());
    vector<unsigned char> enc_msg(sdata.begin(), sdata.end());
    vector<unsigned char> dec_msg(enc_msg.size() - crypto_secretbox_MACBYTES);

    int r =
        crypto_secretbox_open_easy(dec_msg.data(), enc_msg.data(), enc_msg.size(), nonce.data(), sk.data());
    string res;
    if (r == 0)
    {
        res = string(dec_msg.begin(), dec_msg.end());
    }
    return res;
}

string SecretKey::encryptWithPassword(string data, string password)
{
    string key = getKeyFromPass(password);
    return encrypt(data, key);
}

string SecretKey::decryptWithPassword(string data, string password)
{
    string key = getKeyFromPass(password);
    return decrypt(data, key);
}
