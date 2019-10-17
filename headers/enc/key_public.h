#ifndef KEY_PUBLIC_H
#define KEY_PUBLIC_H

//#include <string>
//#include <sstream>
//#include <iostream>
//#include <cstring>

//#include <QDir>
//#include <QDebug>
#include "enc/algorithms/blowfish_crypt.h"
#include "utils/bignumber.h"
#include "enc/algorithms/ecc/eccmath.h"
#include "enc/algorithms/ecc/curves.h"
#include "enc/algorithms/ecc/ellipticpoint.h"

class KeyPublic
{
private:
    ECC::curve curve;
    EllipticPoint pbkey;

public:
    /**
     * @brief Existing keys
     * @param keyPair - [prKey:pubKey]
     */
    KeyPublic(EllipticPoint pubKey);
    KeyPublic(QByteArray pbKey);
    KeyPublic(const KeyPublic &keyPrivate);
    KeyPublic();
    ~KeyPublic()
    {
    }

public: // Cryptor interface
    QByteArray encrypt(const QByteArray &data);

public: // Signer interface
    bool verify(const QByteArray &data, const QByteArray &dsignBase64);

public:
    /**
     * @brief loadPublicKey
     * @param key
     */
    bool loadPublicKey(const QByteArray &keyBase64);

    bool isEmpty();

public:
    /**
     * @brief extractPublicKey
     * @return
     */
    QByteArray extractPublicKey();
    QByteArray getPublicKey();
    QByteArray serialize();
};

#endif // KEY_PUBLIC_H
