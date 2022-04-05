#include "enc/enc_tools.h"

#include <sodium.h>

using std::string, std::vector;

string SecretKey::keygen() {
    vector<unsigned char> sk(crypto_secretbox_KEYBYTES);
    crypto_secretbox_keygen(sk.data());
    string skey = Utils::byteToHexString(sk);
    skey.erase(--skey.end());
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
    (void)rst1; // unused warning
    string skey = Utils::byteToHexString(key);
    skey.erase(--skey.end());
    return skey;
}

string SecretKey::encrypt(const string &msg, const string &secret_key) {
    if (msg.empty() || secret_key.empty())
        qFatal("[SecretKey::encrypt] msg or secret is empty. msg: %s, secret: %s", msg.data(),
               secret_key.data());

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
    if (r == 0) {
        enc_msg.insert(enc_msg.begin(), nonce.begin(), nonce.end());
        res = Utils::byteToHexString(enc_msg);
        res.erase(--res.end());
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

    string sdata = Utils::hexStringToByte(msg);

    string s_nonce = sdata.substr(0, crypto_secretbox_NONCEBYTES);
    sdata.erase(0, crypto_secretbox_NONCEBYTES);

    if (sdata.size() < crypto_secretbox_MACBYTES)
        qFatal("[SecretKey::decrypt] Incorrect msg: %s", msg.data());

    vector<unsigned char> nonce(s_nonce.begin(), s_nonce.end());
    string sks = Utils::hexStringToByte(secret_key);
    vector<unsigned char> sk(sks.begin(), sks.end());
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
    std::vector<unsigned char> sk(crypto_sign_SECRETKEYBYTES);
    std::vector<unsigned char> pk(crypto_sign_PUBLICKEYBYTES);
    crypto_sign_keypair(pk.data(), sk.data());
    std::string m_secretKey = Utils::byteToHexString(sk);
    m_secretKey.erase(--m_secretKey.end());
    std::string m_publicKey = Utils::byteToHexString(pk);
    m_publicKey.erase(--m_publicKey.end());
    return { m_secretKey, m_publicKey };
}

std::string SecretKey::encryptAsymmetric(const std::string &data, const std::string &secret_key,
                                         const std::string &public_key, const std::string &nonce) {
    if (data.empty() || secret_key.empty() || public_key.empty())
        qFatal("[SecretKey::encryptAsymmetric] data, secret or public is empty");

    unsigned long long enc_size = crypto_box_MACBYTES + data.length();
    string pkrs = Utils::hexStringToByte(public_key);
    vector<unsigned char> pkr(pkrs.begin(), pkrs.end());
    string sk = Utils::hexStringToByte(secret_key);
    vector<unsigned char> sks(sk.begin(), sk.end());

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
        res = Utils::byteToHexString(enc_msg);
        res.erase(--res.end());
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

    string sdata = Utils::hexStringToByte(data);
    string pksr = Utils::hexStringToByte(public_key);
    string sk = Utils::hexStringToByte(secret_key);
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
    if (r == 0) {
        res = string(dec_msg.begin(), dec_msg.end());
    }

    if (res.empty())
        qDebug() << "[KeyPrivate::encrypt] res is empty." /*msg:" << data.data()*/
                 << "| secret:" << secret_key.data() << "| public:" << public_key.data()
                 << "| nonce:" << nonce.data();

    return res;
}

QByteArray SecretKey::encryptAsymmetric(const QByteArray &data, const QByteArray &secret_key,
                                        const QByteArray &public_key, const QByteArray &nonce) {
    auto res = encryptAsymmetric(data.toStdString(), secret_key.toStdString(), public_key.toStdString(),
                                 nonce.toStdString());
    return QByteArray::fromStdString(res);
}

QByteArray SecretKey::decryptAsymmetric(const QByteArray &data, const QByteArray &secret_key,
                                        const QByteArray &public_key, const QByteArray &nonce) {
    auto res = decryptAsymmetric(data.toStdString(), secret_key.toStdString(), public_key.toStdString(),
                                 nonce.toStdString());
    return QByteArray::fromStdString(res);
}

QByteArray SecretKey::encryptAsymmetric2(const QByteArray &data, const QByteArray &secret_key,
                                         const QByteArray &public_key, const QByteArray &nonce) {
    if (data.isEmpty() || secret_key.isEmpty() || public_key.isEmpty())
        qFatal("[SecretKey::encryptAsymmetric] data, secret or public is empty");

    auto pkrs = QByteArray::fromHex(public_key.data());
    auto sk = QByteArray::fromHex(secret_key.data());

    vector<unsigned char> xsks(crypto_scalarmult_curve25519_BYTES);
    crypto_sign_ed25519_sk_to_curve25519(xsks.data(), reinterpret_cast<const unsigned char *>(sk.data()));

    vector<unsigned char> xpkr(crypto_scalarmult_curve25519_BYTES);
    int conv_res = crypto_sign_ed25519_pk_to_curve25519(xpkr.data(),
                                                        reinterpret_cast<const unsigned char *>(pkrs.data()));
    Q_UNUSED(conv_res)

    QByteArray enc_msg;
    enc_msg.resize(crypto_box_MACBYTES + data.length());
    QByteArray vnonce;

    if (nonce.size() == crypto_box_NONCEBYTES) {
        vnonce = nonce;
    } else {
        vnonce.resize(crypto_box_NONCEBYTES);
        randombytes_buf(vnonce.data(), vnonce.size());
    }

    int r = crypto_box_easy(reinterpret_cast<unsigned char *>(enc_msg.data()),
                            reinterpret_cast<const unsigned char *>(data.data()), data.size(),
                            reinterpret_cast<const unsigned char *>(vnonce.data()), xpkr.data(), xsks.data());

    QByteArray res;
    if (r == 0) {
        if (nonce.size() != crypto_box_NONCEBYTES) {
            enc_msg.prepend(vnonce);
        }
        res = enc_msg.toHex();
    }

    if (res.isEmpty())
        qDebug() << "[SecretKey::encryptAsymmetric] res is empty. msg:" << data.data()
                 << "| secret:" << secret_key.data() << "| public:" << public_key.data()
                 << "| nonce:" << nonce.data();
    return res;
}
