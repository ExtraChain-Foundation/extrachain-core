#ifndef SYMMETRICCRYPT_H
#define SYMMETRICCRYPT_H
#include "../crypt_interface.h"

class SymmetricCrypt : public ICryptor
{
private:
    QByteArray pubKey;

public:
    SymmetricCrypt(const QByteArray &pubKey);
    QByteArray encrypt(const QByteArray &message);
    QByteArray decrypt(const QByteArray &message);
    QByteArray getPublicKey();
    QByteArray extractPublicKey();
    inline ~SymmetricCrypt(){};
};

#endif // SYMMETRICCRYPT_H
