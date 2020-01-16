#ifndef ACTOR_H
#define ACTOR_H
#include <QDebug>

#include "utils/bignumber.h"
#include "enc/key_private.h"
#include "enc/key_public.h"

#include <utility>
#include <type_traits>
#include "profile/profile.h"
#include "profile/public_profile.h"
/**
 * Acting entity.
 * Users, Smart-contracts
 */
enum actorType
{
    WALLET = 0,
    ACCOUNT = 1,
    COMPANY = 2
};
template <typename T>
class Actor
{
    static_assert((std::is_same<T, KeyPrivate>::value || std::is_same<T, KeyPublic>::value),
                  "Your type is not supported. Only Keys are supported");
    const int FIELDS_SIZE = 4;

private:
    BigNumber id = -1;
    T *key;
    QByteArray hash;
    actorType account;

public:
    bool checkSumValid(QByteArray checkSum)
    {
        return checkSum == getChecksumPubKey();
    }
    inline void setHash(QByteArray hash)
    {
        this->hash = hash;
    }
    inline QByteArray getHash() const
    {
        return this->hash;
    }
    Actor()
    {
        id = 0;
        key = nullptr;
        hash = "";
        account = WALLET;
    }
    Actor(const Actor<T> &copyActor)
    {
        id = copyActor.getId();
        key = new T(*(copyActor.getKey()));
        hash = copyActor.getHash();
        account = static_cast<actorType>(copyActor.getAccount());
    }
    Actor(const QByteArray &serialized)
    {
        this->init(serialized);
    }
    Actor(const BigNumber &id, const QByteArray &keydata, int account)
    {
        this->init(id, keydata, account);
    }
    ~Actor()
    {
        //        delete key;
    }
    Actor operator=(const Actor<T> &copyActor)
    {
        id = copyActor.getId();
        key = new T(*(copyActor.getKey()));
        hash = copyActor.getHash();
        account = static_cast<actorType>(copyActor.getAccount());
        return *this;
    }

private:
    bool isPrivate() const
    {
        return std::is_same<T, KeyPrivate>::value;
    }

    QByteArray getChecksumPubKey()
    {
        QByteArray localPublicKey = "0";
        if (typeid(T) == typeid(KeyPrivate))
        {
            localPublicKey = reinterpret_cast<KeyPrivate *>(key)->getPublicKey();
        }
        else if (typeid(T) == typeid(KeyPublic))
        {
            localPublicKey = reinterpret_cast<KeyPublic *>(key)->getPublicKey();
        }
        else
            return "0";

        QString hash = Utils::calcKeccak(localPublicKey);
        while (hash.size() < localPublicKey.size())
            hash = hash.append(hash);
        for (int i = 0; i < localPublicKey.size(); i++)
        {
            if (QString(hash[i]).toInt(nullptr, 16) >= 8)
            {
                localPublicKey[i] = localPublicKey.toUpper()[i];
            }
        }
        return localPublicKey;
    }

public:
    /**
     * @brief initial construction
     * @param serialized
     */
    bool init(const QByteArray &serialized)
    {
        if (!serialized.isEmpty())
        {
            if (isPrivate())
            {
                // old method of serialize
                //                QList<QByteArray> list = Serialization::deserialize(
                //                    serialized, Serialization::DEFAULT_FIELD_SPLITTER);
                QList<QByteArray> list = Serialization::universalDeserialize(serialized, FIELDS_SIZE);

                this->id = BigNumber(list.at(0));
                this->key = new T(list.at(1));
                account = static_cast<actorType>(list.at(2).toInt());
            }
            else
            {
                QList<QByteArray> list = Serialization::universalDeserialize(serialized, FIELDS_SIZE);
                if (list.length() >= 2)
                {
                    this->id = BigNumber(list.at(0));
                    this->key = new T(list.at(1));
                    this->account = static_cast<actorType>(list.at(2).toInt());
                }
            }
            QByteArray hashData(toString().toUtf8());
            hash = Utils::calcKeccak(hashData);
            return true;
        }
        else
        {
            qDebug() << "WARNING!:: Actor::init(const QByteArray &serialized) serialized IS "
                        "EMPTY!";
            return false;
        }
    }
    /**
     * @brief initial construction of new Actor
     * @param id
     */
    bool init(int account)
    {
        if (isPrivate())
        {
            key = new T();
            if (typeid(T) == typeid(KeyPrivate))
            {
                KeyPrivate *k = reinterpret_cast<KeyPrivate *>(key);
                k->generate();
                QByteArray hashPubKey = Utils::calcKeccak(k->getPublicKey());
                if (hashPubKey.size() >= 20)
                {
                    id = BigNumber(hashPubKey.mid(hashPubKey.size() - 20));
                }
                else
                    qDebug() << "[Error] Actor.h func InitNew. Error size of hashPubKey";
            }
            QByteArray hashData(toString().toUtf8());
            hash = Utils::calcKeccak(hashData);
            this->account = static_cast<actorType>(account);
            return true;
        }
        else
            return false;
    }
    /**
     * @brief initial construction. Can be used to create public actor.
     * @param id
     * @param keydata - (private/public key)
     */
    bool init(const BigNumber &id, const QByteArray &keydata, int account)
    {
        this->id = id;
        this->key = new T(keydata);
        this->account = static_cast<actorType>(account);
        return true;
    }

    bool isEmpty() const
    {
        if (key == nullptr)
            return true;
        if (!isPrivate())
        {
            KeyPublic *pbKey = reinterpret_cast<KeyPublic *>(key);
            return pbKey->isEmpty();
        }
        return id == BigNumber(-1) || key == nullptr;
    }

    /**
     * @brief serialize actor to QByteArray
     * ecdsa_private - has pubkey and prkey
     * ecdsa_public - has pubkey only
     * @return serialized actors
     */
    QByteArray serialize() const
    {
        QList<QByteArray> list;
        if (key != nullptr)
        {
            QByteArray pubKey = key->extractPublicKey();
            //  QByteArray
            if (isPrivate())
            {
                // key_private
                KeyPrivate *prKey = reinterpret_cast<KeyPrivate *>(key);
                QList<QByteArray> list;

                qDebug() << this->id.toActorId() << prKey->serialize() << pubKey;

                list << this->id.toActorId() << prKey->serialize() << QByteArray::number(account);
                //
                QByteArray serialized = Serialization::universalSerialize(list, FIELDS_SIZE);
                return serialized;
            }
            else
            {
                // key_public
                list << id.toActorId() << pubKey << QByteArray::number(account);
            }
        }
        else
        {
            list << id.toActorId();
        }
        QByteArray serialized = Serialization::universalSerialize(list, 4);
        return serialized;
    }

    QString toString() const
    {
        QList<QByteArray> list;
        list << "id:" + id.toActorId();
        if (key != nullptr)
        {
            list << "pub_key:" + key->getPublicKey();
            if (isPrivate())
            {
                list << "pr_key:" + reinterpret_cast<KeyPrivate *>(key)->getPrivateKey().toByteArray();
            }
        }
        else
        {
            list << "pub_key:";
        }
        list << QByteArray::number(account);
        //        return Serialization::serializeString(list,
        //        Serialization::ACTOR_FIELD_SPLITTER);//
        QByteArray serialized = Serialization::universalSerialize(list, FIELDS_SIZE);
        return QString(serialized);
    }
    PublicProfile profile()
    {
        QByteArray pathToFolder = ChatStorage::STORED_CHATS + id.toActorId() + "/profile/";
        return PublicProfile(id.toActorId(), pathToFolder);
    }

public:
    bool operator==(const Actor<T> &other)
    {
        T *otherKey = other.getKey();
        return this->getId() == other.getId() && *key == *otherKey;
    }

    bool operator<(const Actor<T> other)
    {
        if (id < other.getId())
        {
            return true;
        }
        return false;
    }

    BigNumber getId() const
    {
        return id;
    }

    T *getKey() const
    {
        return key;
    }

    actorType getAccount() const
    {
        return account;
    }

    void setAccount(bool value)
    {
        account = value;
    }

    Actor<KeyPublic> convertToPublic() const
    {
        return isPrivate() ? Actor<KeyPublic>(getId(), getKey()->extractPublicKey(), getAccount())
                           : Actor<KeyPublic>();
    }
};

inline bool operator<(const Actor<KeyPublic> &l, const Actor<KeyPublic> &r)
{
    return l.getId() < r.getId();
}
inline bool operator<(const Actor<KeyPrivate> &l, const Actor<KeyPrivate> &r)
{
    return l.getId() < r.getId();
}

#endif // ACTOR_H
