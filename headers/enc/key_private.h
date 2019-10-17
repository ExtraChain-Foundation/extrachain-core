#ifndef KEY_PRIVATE_H
#define KEY_PRIVATE_H

//#include <string>
//#include <sstream>
//#include <iostream>
//#include <cstring>
//#include <QDir>

#include <QDebug>
#include "utils/bignumber.h"
#include "enc/algorithms/blowfish_crypt.h"
#include "enc/algorithms/ecc/ellipticpoint.h"
#include "enc/algorithms/ecc/curves.h"
#include "enc/algorithms/ecc/eccmath.h"

class KeyPrivate
{
private:
    ECC::curve curve;
    BigNumber prkey;
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

public:
    EllipticPoint generate();

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
    BigNumber extractPrivateKey();
    /**
     * @brief extractPublicKey
     * @return
     */
    QByteArray extractPublicKey();
    BigNumber getPrivateKey();
    QByteArray getPublicKey();
    QByteArray serialize();
};

#endif // KEY_PRIVATE_H
