#include "enc/crypt_manager.h"

CryptManager::CryptManager(AccountController *accountControler, QObject *parent)
    : QObject(parent)
{
    this->accountControler = accountControler;
}

CryptManager::~CryptManager()
{
    delete accountControler;
}

QByteArray CryptManager::encrypt(const QByteArray &data, const BigNumber &actorId)
{
    return accountControler->getActorIndex()->getActor(actorId).getKey()->encrypt(data);
}

QByteArray CryptManager::decrypt(const QByteArray &data)
{
    return accountControler->getMainActor()->getKey()->decrypt(data);
}

QByteArray CryptManager::decrypt(const QByteArray &data, const QByteArray &key)
{
    Actor<KeyPrivate> actor(key);
    return actor.getKey()->decrypt(data);
}

QByteArray CryptManager::encrypt(const QByteArray &data, const QByteArray &key)
{
    Actor<KeyPublic> key_t(key);
    return key_t.getKey()->encrypt(data);
}

QByteArray CryptManager::decrypt(const QByteArray &data, const BigNumber &actorId)
{
    return accountControler->getActor(actorId).getKey()->decrypt(data);
}

void CryptManager::process()
{
}
/*
 * namespace crypting
 * class CryptManager
 * public slot recieveData
 * number of request
 * data for encrypting or decripting
 * key {public key, private key, actorId, empty if needs decrypt for main actor Id}
 */
void CryptManager::recieveData(int place, int request, QByteArray data, QByteArray key)
{
    switch (request)
    {
    case crypting::ENCRYPT_USE_KEY_REQUEST:
    {
        emit sendEncryptData(place, crypting::ENCRYPT_DATA_RESPONSE, encrypt(data, key));
        break;
    }
    case crypting::ENCRYPT_USE_ACTOR_REQUEST:
    {
        emit sendEncryptData(place, crypting::ENCRYPT_DATA_RESPONSE, encrypt(data, BigNumber(key)));
        break;
    }
    case crypting::DECRYPT_USE_KEY__REQUEST:
    {
        emit sendEncryptData(place, crypting::DECRYPT_DATA_RESPONSE, decrypt(data, key));
        break;
    }
    case crypting::DECRYPT_USE_ACTOR__REQUEST:
    {
        emit sendEncryptData(place, crypting::DECRYPT_DATA_RESPONSE, decrypt(data, BigNumber(key)));
        break;
    }
    case crypting::DECRYPT_USE_MAIN_ACTOR_REQUEST:
    {
        emit sendEncryptData(place, crypting::DECRYPT_DATA_RESPONSE, decrypt(data));
        break;
    }
    }
}
