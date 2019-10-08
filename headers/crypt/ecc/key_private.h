#ifndef KEY_PRIVATE_H
#define KEY_PRIVATE_H

//#include <string>
//#include <sstream>
//#include <iostream>
//#include <cstring>
//#include <QDir>

#include <QDebug>
#include "utils/bignumber.h"
#include "crypt/ecc/ellipticpoint.h"
#include "crypt/ecc/curves.h"
#include "crypt/ecc/math.h"

class KeyPrivate
{
private:
    ECC::curve curve;
    QByteArray prkey;
    EllipticPoint pbkey;

public:
    /**
     * @brief New keys
     */
    KeyPrivate();
    /**
     * @brief Existing keys
     * @param keyPair - [prKey:pubKey]
     */
    KeyPrivate(const QByteArray &keyPrivate);
    KeyPrivate(const KeyPrivate &keyPrivate);
    ~KeyPrivate();

public: // Cryptor interface
    QByteArray encrypt(const QByteArray &data);
    QByteArray decrypt(const QByteArray &data);

public: // Signer interface
    QByteArray sign(const QByteArray &data);
    bool verify(const QByteArray &data, const QByteArray &dsignBase64);

public:
    /**
     * @brief extractPrivateKey
     * @return
     */
    QByteArray extractPrivateKey();
    /**
     * @brief extractPublicKey
     * @return
     */
    QByteArray extractPublicKey();
    QByteArray getPrivateKey();
    QByteArray getPublicKey();
    QByteArray serialize();
};

#endif // KEY_PRIVATE_H
