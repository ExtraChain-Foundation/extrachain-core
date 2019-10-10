#ifndef CRYPT_INTERFACE_H
#define CRYPT_INTERFACE_H

#include <QString>

class ICryptor
{
public:
    virtual ~ICryptor() = 0;

public:
    static QByteArray encrypt(const QByteArray key, const QByteArray &data)
    {
        Q_UNUSED(key)
        Q_UNUSED(data)
        return "";
    }
    static QByteArray decrypt(const QByteArray key, const QByteArray &data)
    {
        Q_UNUSED(key)
        Q_UNUSED(data)
        return "";
    }

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
#endif // CRYPT_INTERFACE_H
