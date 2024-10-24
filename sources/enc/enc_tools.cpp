#include "enc/enc_tools.h"

KeyBytes Cryptography::keygen() {
    KeyBytes sk;
    crypto_secretbox_keygen(sk.data());
    // string skey = std::string(sk.begin(), sk.end());
    // skey.erase(--skey.end());
    return sk;
}

KeyPass Cryptography::getKeyPassFromPassword(const std::string &pass, const Salt &salt) {
    if (pass.empty()) {
        qFatal("[SecretKey::getKeyFromPass] pass is empty. salt: %s", salt.data());
    }

    Salt vsalt;
    if (salt.empty()) {
        std::fill(vsalt.begin(), vsalt.end(), '0');
    } else {
        vsalt = salt;
    }

    KeyPass key;
    int     rst1 = crypto_pwhash(
        key.data(),
        key.size(),
        pass.data(),
        pass.size(),
        vsalt.data(),
        crypto_pwhash_OPSLIMIT_INTERACTIVE,
        crypto_pwhash_MEMLIMIT_INTERACTIVE,
        crypto_pwhash_ALG_DEFAULT);
    if (rst1 != 0) {
        qFatal("Incorrect getKeyFromPass");
    }
    // string skey = std::string(key.begin(), key.end());
    // skey.erase(--skey.end());
    return key;
}

Signature Cryptography::sign(const Bytes &data, const PrivateKey &secret_key) {
    if (data.empty() || secret_key.empty()) {
        qFatal(
            "[SecretKey::sign] data or secret is empty. data: %s, secret: %s",
            data.data(),
            secret_key.data());
    }

    Signature sig;
    crypto_sign_detached(sig.data(), NULL, data.data(), data.size(), secret_key.data());
    return sig;
}

bool Cryptography::verify(const Bytes &data, const PublicKey &public_key, const Signature &signature) {
    if (data.empty() || public_key.empty() || signature.empty()) {
        qCritical().noquote().nospace()
            << "[SecretKey::verify] data or secret is empty. data: '" << data << "', public: '"
            << public_key.data() << "', signature: '" << signature.data() << "'";
        return false;
    }

    int res = crypto_sign_verify_detached(signature.data(), data.data(), data.size(), public_key.data());
    return res == 0;
}

Bytes Cryptography::encrypt(const Bytes &data, const KeyPass &secret_key) {
    if (data.empty() || secret_key.empty()) {
        qFatal(
            "[SecretKey::encrypt] data or secret is empty. data: %s, secret: %s",
            data.data(),
            secret_key.data());
    }

    unsigned long long enc_size = crypto_secretbox_MACBYTES + data.size();
    Bytes              encrypted(enc_size);

    Nonce nonce;
    randombytes_buf(nonce.data(), nonce.size());

    int r =
        crypto_secretbox_easy(encrypted.data(), data.data(), data.size(), nonce.data(), secret_key.data());

    if (r != 0) {
        qDebug() << "[SecretKey::encrypt] Encryption failed";
        return Bytes();
    }

    encrypted.insert(encrypted.begin(), nonce.begin(), nonce.end());
    return encrypted;
}

Bytes Cryptography::decrypt(const Bytes &encrypted_data, const KeyPass &secret_key) {
    if (encrypted_data.empty() || secret_key.empty()) {
        qFatal(
            "[SecretKey::decrypt] data or secret is empty. data: %s, secret: %s",
            encrypted_data.data(),
            secret_key.data());
    }

    const size_t minimum_size = crypto_secretbox_NONCEBYTES + crypto_secretbox_MACBYTES;
    if (encrypted_data.size() < minimum_size) {
        qFatal("[SecretKey::decrypt] Encrypted data too short");
    }

    Nonce nonce;
    std::copy_n(encrypted_data.begin(), crypto_secretbox_NONCEBYTES, nonce.begin());

    const Bytes encrypted_message(encrypted_data.begin() + crypto_secretbox_NONCEBYTES, encrypted_data.end());

    if (encrypted_message.size() < crypto_secretbox_MACBYTES) {
        qFatal("[SecretKey::decrypt] Incorrect msg size");
    }

    Bytes decrypted_message(encrypted_message.size() - crypto_secretbox_MACBYTES);

    int r = crypto_secretbox_open_easy(
        decrypted_message.data(),
        encrypted_message.data(),
        encrypted_message.size(),
        nonce.data(),
        secret_key.data());

    if (r != 0) {
        qDebug() << "[SecretKey::decrypt] Decryption failed";
        return Bytes();
    }

    return decrypted_message;
}

std::string Cryptography::encrypt(const std::string &data, const KeyPass &secret_key) {
    auto res = encrypt(ByteArray(data).toBytes(), secret_key);
    return ByteArray(res).toString();
}

std::string Cryptography::decrypt(const std::string &data, const KeyPass &secret_key) {
    auto res = decrypt(ByteArray(data).toBytes(), secret_key);
    return ByteArray(res).toString();
}

Bytes Cryptography::encryptWithPassword(const Bytes &data, const std::string &password) {
    auto key = getKeyPassFromPassword(password);
    return encrypt(data, key);
}

Bytes Cryptography::decryptWithPassword(const Bytes &data, const std::string &password) {
    auto key = getKeyPassFromPassword(password);
    return decrypt(data, key);
}

std::pair<PrivateKey, PublicKey> Cryptography::createAsymmetricPair() {
    PrivateKey sk;
    PublicKey  pk;
    crypto_sign_keypair(pk.data(), sk.data());
    return { sk, pk };
}

Bytes Cryptography::encryptAsymmetric(
    const Bytes      &data,
    const PrivateKey &secret_key,
    const PublicKey  &public_key,
    const Nonce      &nonce) {
    if (data.empty()) {
        qFatal("[SecretKey::encryptAsymmetric] data is empty");
    }

    Curve25519Key x_secret_key;
    Curve25519Key x_public_key;

    int res1 = crypto_sign_ed25519_sk_to_curve25519(x_secret_key.data(), secret_key.data());
    int res2 = crypto_sign_ed25519_pk_to_curve25519(x_public_key.data(), public_key.data());

    // Подготовка nonce
    Nonce working_nonce;
    if (nonce.size() == crypto_box_NONCEBYTES) {
        working_nonce = nonce;
    } else {
        randombytes_buf(working_nonce.data(), working_nonce.size());
    }

    Bytes encrypted_message(crypto_box_MACBYTES + data.size());

    if (crypto_box_easy(
            encrypted_message.data(),
            data.data(),
            data.size(),
            working_nonce.data(),
            x_public_key.data(),
            x_secret_key.data())
        != 0) {
        qDebug() << "[SecretKey::encryptAsymmetric] Encryption failed";
        return Bytes {};
    }

    if (nonce.size() != crypto_box_NONCEBYTES) {
        Bytes result(working_nonce.size() + encrypted_message.size());
        std::copy(working_nonce.begin(), working_nonce.end(), result.begin());
        std::copy(encrypted_message.begin(), encrypted_message.end(), result.begin() + working_nonce.size());
        return result;
    }

    return encrypted_message;
}

Bytes Cryptography::decryptAsymmetric(
    const Bytes      &encrypted_data,
    const PrivateKey &secret_key,
    const PublicKey  &public_key,
    const Nonce      &provided_nonce) {
    if (encrypted_data.empty()) {
        qFatal("[SecretKey::decryptAsymmetric] encrypted data is empty");
    }

    Nonce working_nonce;
    Bytes encrypted_message;

    if (provided_nonce.size() == crypto_box_NONCEBYTES) {
        working_nonce     = provided_nonce;
        encrypted_message = encrypted_data;
    } else {
        if (encrypted_data.size() < crypto_box_NONCEBYTES) {
            qFatal("[SecretKey::decryptAsymmetric] Data too short to contain nonce");
        }
        std::copy_n(encrypted_data.begin(), crypto_box_NONCEBYTES, working_nonce.begin());
        encrypted_message = Bytes(encrypted_data.begin() + crypto_box_NONCEBYTES, encrypted_data.end());
    }

    if (encrypted_message.size() < crypto_box_MACBYTES) {
        qFatal("[SecretKey::decryptAsymmetric] Encrypted message too short");
    }

    Curve25519Key x_secret_key;
    Curve25519Key x_public_key;

    int res1 = crypto_sign_ed25519_sk_to_curve25519(x_secret_key.data(), secret_key.data());
    int res2 = crypto_sign_ed25519_pk_to_curve25519(x_public_key.data(), public_key.data());

    Bytes decrypted_message(encrypted_message.size() - crypto_box_MACBYTES);

    if (crypto_box_open_easy(
            decrypted_message.data(),
            encrypted_message.data(),
            encrypted_message.size(),
            working_nonce.data(),
            x_public_key.data(),
            x_secret_key.data())
        != 0) {
        qDebug() << "[SecretKey::decryptAsymmetric] Decryption failed";
        return Bytes {};
    }

    return decrypted_message;
}
