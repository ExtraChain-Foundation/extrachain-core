#ifndef XORENCRYPT_H
#define XORENCRYPT_H
#include "enc/crypt_interface.h"

class XOREncrypt : ICryptor
{
public:
    static QByteArray encrypt(QByteArray key, const QByteArray &message);
    static QByteArray decrypt(QByteArray key, const QByteArray &message);
    QByteArray getPublicKey();
    QByteArray extractPublicKey();
    bool loadPublicKey(const QByteArray &key);
    inline ~XOREncrypt()
    {
    }

private:
    XOREncrypt() = default;
};

#endif // XORENCRYPT_H
