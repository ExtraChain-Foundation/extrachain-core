#include "enc/enc_tools.h"

#include <sodium.h>

using std::string, std::vector;

string SecretKey::keygen() {
    vector<unsigned char> sk(crypto_secretbox_KEYBYTES);
    crypto_secretbox_keygen(sk.data());
    string skey = std::string(sk.begin(), sk.end());
    // skey.erase(--skey.end());
    return skey;
}

string SecretKey::getKeyFromPass(const string &pass, const string &salt) {
    vector<unsigned char> vsalt(crypto_pwhash_SALTBYTES);
    if (salt.empty() || salt.size() < crypto_pwhash_SALTBYTES) {
        std::fill(vsalt.begin(), vsalt.end(), '0');
    } else {
        vsalt = vector<unsigned char>(salt.begin(), salt.end());
    }
    vector<unsigned char> key(crypto_box_SEEDBYTES);
    int rst1 = crypto_pwhash(key.data(), key.size(), pass.data(), pass.size(), vsalt.data(),
                             crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE,
                             crypto_pwhash_ALG_DEFAULT);
    if (rst1 != 0) {
        qFatal("Incorrect getKeyFromPass");
    }
    string skey = std::string(key.begin(), key.end());
    // skey.erase(--skey.end());
    return skey;
}

std::string SecretKey::sign(const std::string &data, const std::string &secret_key) {
    std::vector<unsigned char> sk(secret_key.begin(), secret_key.end());
    std::vector<unsigned char> vmsg(data.begin(), data.end());
    std::vector<unsigned char> vsig(crypto_sign_BYTES);
    crypto_sign_detached(vsig.data(), NULL, vmsg.data(), vmsg.size(), sk.data());
    std::string sig = std::string(vsig.begin(), vsig.end());
    return base64_encode(sig);
}

bool SecretKey::verify(const std::string &data, const std::string &public_key, const std::string &signature) {
    std::string sig = base64_decode(signature);
    std::vector<unsigned char> pk(public_key.begin(), public_key.end());
    std::vector<unsigned char> vmsg(data.begin(), data.end());
    std::vector<unsigned char> vsig(sig.begin(), sig.end());
    int res = crypto_sign_verify_detached(vsig.data(), vmsg.data(), vmsg.size(), pk.data());
    return res == 0;
}

string SecretKey::encrypt(const string &msg, const string &secret_key) {
    if (msg.empty() || secret_key.empty())
        qFatal("[SecretKey::encrypt] msg or secret is empty. msg: %s, secret: %s", msg.data(),
               secret_key.data());

    unsigned long long enc_size = crypto_secretbox_MACBYTES + msg.length();
    vector<unsigned char> sk(secret_key.begin(), secret_key.end());

    vector<unsigned char> enc_msg(enc_size);
    vector<unsigned char> dec_msg(msg.begin(), msg.end());
    vector<unsigned char> nonce;
    nonce.resize(crypto_secretbox_NONCEBYTES);
    randombytes_buf(nonce.data(), nonce.size());
    int r = crypto_secretbox_easy(enc_msg.data(), dec_msg.data(), dec_msg.size(), nonce.data(), sk.data());
    string res;
    if (r == 0) {
        enc_msg.insert(enc_msg.begin(), nonce.begin(), nonce.end());
        res = std::string(enc_msg.begin(), enc_msg.end());
    }

    if (res.empty())
        qDebug() << "[SecretKey::encrypt] res is empty. msg:" << msg.data()
                 << "| secret:" << secret_key.data();
    return res;
}

string SecretKey::decrypt(const string &msg, const string &secret_key) {
    if (msg.empty() || secret_key.empty())
        qFatal("[SecretKey::decrypt] msg or secret is empty. msg: %s, secret: %s", msg.data(),
               secret_key.data());

    string sdata = msg;

    string s_nonce = sdata.substr(0, crypto_secretbox_NONCEBYTES);
    sdata.erase(0, crypto_secretbox_NONCEBYTES);

    if (sdata.size() < crypto_secretbox_MACBYTES)
        qFatal("[SecretKey::decrypt] Incorrect msg: %s", msg.data());

    vector<unsigned char> nonce(s_nonce.begin(), s_nonce.end());
    vector<unsigned char> sk(secret_key.begin(), secret_key.end());
    vector<unsigned char> enc_msg(sdata.begin(), sdata.end());
    vector<unsigned char> dec_msg(enc_msg.size() - crypto_secretbox_MACBYTES);

    int r =
        crypto_secretbox_open_easy(dec_msg.data(), enc_msg.data(), enc_msg.size(), nonce.data(), sk.data());
    string res;
    if (r == 0) {
        res = string(dec_msg.begin(), dec_msg.end());
    }

    if (res.empty())
        qDebug() << "[SecretKey::decrypt] res is empty. msg:" << msg.data()
                 << "| secret:" << secret_key.data();
    return res;
}

string SecretKey::encryptWithPassword(const string &data, const string &password) {
    string key = getKeyFromPass(password);
    return encrypt(data, key);
}

string SecretKey::decryptWithPassword(const string &data, const string &password) {
    string key = getKeyFromPass(password);
    return decrypt(data, key);
}

std::pair<std::string, std::string> SecretKey::createAsymmetricPair() {
    std::vector<uint8_t> sk(crypto_sign_SECRETKEYBYTES);
    std::vector<uint8_t> pk(crypto_sign_PUBLICKEYBYTES);
    crypto_sign_keypair(pk.data(), sk.data());
    std::string m_secretKey = std::string(sk.begin(), sk.end());
    std::string m_publicKey = std::string(pk.begin(), pk.end());
    return { m_secretKey, m_publicKey };
}

std::string SecretKey::encryptAsymmetric(const std::string &data, const std::string &secret_key,
                                         const std::string &public_key, const std::string &nonce) {
    if (data.empty() || secret_key.empty() || public_key.empty())
        qFatal("[SecretKey::encryptAsymmetric] data, secret or public is empty");

    unsigned long long enc_size = crypto_box_MACBYTES + data.length();
    vector<unsigned char> pkr(public_key.begin(), public_key.end());
    vector<unsigned char> sks(secret_key.begin(), secret_key.end());

    vector<unsigned char> xsks(crypto_scalarmult_curve25519_BYTES);
    crypto_sign_ed25519_sk_to_curve25519(xsks.data(), sks.data());

    vector<unsigned char> xpkr(crypto_scalarmult_curve25519_BYTES);
    int conv_res = crypto_sign_ed25519_pk_to_curve25519(xpkr.data(), pkr.data());
    Q_UNUSED(conv_res)

    vector<unsigned char> enc_msg(enc_size);
    vector<unsigned char> dec_msg(data.begin(), data.end());
    vector<unsigned char> vnonce;
    vnonce.resize(crypto_box_NONCEBYTES);

    if (nonce.size() == crypto_box_NONCEBYTES) {
        vnonce = vector<unsigned char>(nonce.begin(), nonce.end());
    } else {
        randombytes_buf(vnonce.data(), vnonce.size());
    }

    int r =
        crypto_box_easy(enc_msg.data(), dec_msg.data(), data.size(), vnonce.data(), xpkr.data(), xsks.data());

    string res;
    if (r == 0) {
        if (nonce.size() != crypto_box_NONCEBYTES) {
            enc_msg.insert(enc_msg.begin(), vnonce.begin(), vnonce.end());
        }
        res = std::string(enc_msg.begin(), enc_msg.end());
    }

    if (res.empty())
        qDebug() << "[SecretKey::encryptAsymmetric] res is empty. msg:" << data.data()
                 << "| secret:" << secret_key.data() << "| public:" << public_key.data()
                 << "| nonce:" << nonce.data();
    return res;
}

std::string SecretKey::decryptAsymmetric(const std::string &data, const std::string &secret_key,
                                         const std::string &public_key, const std::string &nonce) {
    if (data.empty() || secret_key.empty() || public_key.empty())
        qFatal("[SecretKey::decryptAsymmetric] data, secret or public is empty");

    string sdata = data;
    vector<unsigned char> vnonce;

    if (nonce.size() == crypto_box_NONCEBYTES) {
        vnonce = vector<unsigned char>(nonce.begin(), nonce.end());
    } else {
        string s_nonce = sdata.substr(0, crypto_box_NONCEBYTES);
        sdata.erase(0, crypto_box_NONCEBYTES);
        vnonce = vector<unsigned char>(s_nonce.begin(), s_nonce.end());
    }

    if (sdata.size() < crypto_secretbox_MACBYTES) {
        qCritical() << "Critical: [SecretKey::decryptAsymmetric] Incorrect msg" << sdata.size()
                    << crypto_secretbox_MACBYTES;
        return "";
        qFatal("[SecretKey::decryptAsymmetric] Incorrect msg");
    }

    vector<unsigned char> skr(secret_key.begin(), secret_key.end());
    vector<unsigned char> pks(public_key.begin(), public_key.end());

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
    if (r == 0) {
        res = string(dec_msg.begin(), dec_msg.end());
    }

    if (res.empty())
        qDebug() << "[KeyPrivate::encrypt] res is empty." /*msg:" << data.data()*/
                 << "| secret:" << secret_key.data() << "| public:" << public_key.data()
                 << "| nonce:" << nonce.data();

    return res;
}
