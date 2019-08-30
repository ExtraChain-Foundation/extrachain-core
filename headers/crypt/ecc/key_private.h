#ifndef KEY_PRIVATE_H
#define KEY_PRIVATE_H

//#include <string>
//#include <sstream>
//#include <iostream>
//#include <cstring>
//#include <QDir>

#include <QDebug>
#include "EllipticPoints.h"
#include "utils/bignumber.h"
#include "crypt/ecc/ecc.h"

class KeyPrivate
{
private:
    QByteArray prkey;
    EllipticPoints pbkey;

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
     * @brief loadPrivateKey
     * @param key
     */
    /**
     * @brief extractPrivateKey
     * @return
     */
    inline QByteArray extractPrivateKey()
    {
        return this->prkey;
    }
    /**
     * @brief extractPublicKey
     * @return
     */
    QByteArray extractPublicKey();
    inline QByteArray getPrivateKey()
    {
        return extractPrivateKey();
    }
    /**
     * @brief getPublicKey
     * @return
     */
    EllipticPoints getPublicPoint()
    {
        return this->pbkey;
    }
    QByteArray getPublicKey();

private:
    QByteArray getPrivateX();
    QByteArray getPrivateY();
};

#endif // KEY_PRIVATE_H
