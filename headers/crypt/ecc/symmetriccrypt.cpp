#include "symmetriccrypt.h"

SymmetricCrypt::SymmetricCrypt(const QByteArray &pubKey)
{
    this->pubKey = pubKey;
}

QByteArray SymmetricCrypt::encrypt(const QByteArray &message)
{
    QByteArray resMessage = "";

    if (message.size() < pubKey.size())
    {
        for (int i = 0; i < message.size(); i++)
            resMessage.append((char)(message[i] ^ pubKey[i]));
        return resMessage;
    }
    else if (message.size() > pubKey.size())
    {
        for (int i = 0; i < pubKey.size() - message.size(); i++)
            pubKey.append("0");
        for (int i = 0; i < message.size(); i++)
            resMessage.append((char)(message[i] ^ pubKey[i]));
        return resMessage;
    }

    for (int i = 0; i < message.size(); i++)
        resMessage.append((char)(message[i] ^ pubKey[i]));

    return resMessage;
}

QByteArray SymmetricCrypt::decrypt(const QByteArray &message)
{
    return this->encrypt(message);
}

QByteArray SymmetricCrypt::getPublicKey()
{
    return this->pubKey;
}

QByteArray SymmetricCrypt::extractPublicKey()
{
    return this->pubKey;
}
