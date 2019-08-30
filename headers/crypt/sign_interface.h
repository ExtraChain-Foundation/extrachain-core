#ifndef SIGN_INTERFACE_H
#define SIGN_INTERFACE_H

#include <QString>

class IApprover
{
public:
    virtual ~IApprover() = 0;
public:
    /**
     * @brief creates digital signature from data
     * @param data - serialized object
     * @return digitalSignature encoded with Base64
     */
    virtual QByteArray sign(const QByteArray &data) = 0;
    /**
     * @brief verifies that digital signature and data is correct
     * @param data - serialized object
     * @param dsign - digitalSignature encoded with Base64
     * @return true, if digitalSignature is right
     */
    virtual bool verify(const QByteArray &data, const QByteArray &dsignBase64) = 0;
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

inline IApprover::~IApprover() {}

#endif // SIGN_INTERFACE_H
