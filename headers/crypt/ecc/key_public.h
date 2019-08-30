#ifndef KEY_PUBLIC_H
#define KEY_PUBLIC_H

//#include <string>
//#include <sstream>
//#include <iostream>
//#include <cstring>

//#include <QDir>
//#include <QDebug>

#include "EllipticPoints.h"
#include "utils/bignumber.h"
#include "crypt/ecc/ecc.h"

class KeyPublic
{
private:
    EllipticPoints pbkey;

public:
    /**
     * @brief Existing keys
     * @param keyPair - [prKey:pubKey]
     */
    KeyPublic(EllipticPoints pubKey)
    {
        this->pbkey = pubKey;
    }
    KeyPublic(QByteArray pbKey);
    KeyPublic(const KeyPublic &keyPrivate);
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

public:
    /**
     * @brief extractPublicKey
     * @return
     */
    QByteArray extractPublicKey();

    bool operator==(KeyPublic &other);

public:
    /**
     * @brief getPublicKey
     * @return
     */
    EllipticPoints getPublicPoint()
    {
        return this->pbkey;
    }
    QByteArray getPublicKey();
};

#endif // KEY_PUBLIC_H
