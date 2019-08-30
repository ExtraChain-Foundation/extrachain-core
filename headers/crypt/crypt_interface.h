#ifndef CRYPT_INTERFACE_H
#define CRYPT_INTERFACE_H

#include <QString>

class ICryptor
{
public:
    virtual ~ICryptor() = 0;
public:
    virtual QByteArray encrypt(const QByteArray &data) = 0;
    virtual QByteArray decrypt(const QByteArray &data) = 0;
    /**
     * @brief setter for public key
     * @param publicKey
     */
    virtual bool loadPublicKey(const QByteArray &key) = 0;
    /**
     * @brief Serialize method
     * @return public key string
     */
    virtual QByteArray extractPublicKey() = 0;
    /**
     * @brief gets key readable
     * @return public key string
     */
    virtual QByteArray getPublicKey() = 0;
};

inline ICryptor::~ICryptor() {}
#endif // CRYPT_INTERFACE_H
