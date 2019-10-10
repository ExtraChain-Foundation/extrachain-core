#include "enc/algorithms/xor_encrypt.h"

QByteArray XOREncrypt::encrypt(QByteArray key, const QByteArray &message)
{
    QByteArray resMessage = "";

    if (message.size() < key.size())
    {
        for (int i = 0; i < message.size(); i++)
            resMessage.append((char)(message[i] ^ key[i]));
        return resMessage;
    }
    else if (message.size() > key.size())
    {
        for (int i = 0; i < key.size() - message.size(); i++)
            key.append("0");
        for (int i = 0; i < message.size(); i++)
            resMessage.append((char)(message[i] ^ key[i]));
        return resMessage;
    }

    for (int i = 0; i < message.size(); i++)
        resMessage.append((char)(message[i] ^ key[i]));

    return resMessage;
}

QByteArray XOREncrypt::decrypt(QByteArray key, const QByteArray &message)
{
    return encrypt(key, message);
}
