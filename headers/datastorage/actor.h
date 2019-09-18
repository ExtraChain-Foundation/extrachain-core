#ifndef ACTOR_H
#define ACTOR_H
#include <QDebug>

#include "utils/bignumber.h"
#include "crypt/ecc/key_private.h"
#include "crypt/ecc/key_public.h"

#include <utility>
#include <type_traits>
#include <datastorage/profile.h>
/**
 * Acting entity.
 * Users, Smart-contracts
 */

template <typename T>
class Actor
{
    static_assert((std::is_same<T, KeyPrivate>::value || std::is_same<T, KeyPublic>::value),
                  "Your type is not supported. Only Keys are supported");
    const int FIELDS_SIZE = 4;

private:
    BigNumber id;
    T *key;
    QByteArray hash; // to encrypt private key (email and pass)
    bool account;

public:
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
        id = BigNumber(-1);
        key = nullptr;
        hash = "";
        account = true;
    }
    Actor(const Actor<T> &copyActor)
    {
        id = copyActor.getId();
        key = new T(*(copyActor.getKey()));
        hash = copyActor.getHash();
        account = copyActor.getAccount();
    }
    Actor(const QByteArray &serialized)
    {
        this->init(serialized);
    }
    Actor(const BigNumber &id, const QByteArray &keydata, bool account)
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
        account = copyActor.getAccount();
        return *this;
    }

private:
    bool isPrivate() const
    {
        return std::is_same<T, KeyPrivate>::value;
    }

public:
    /**
     * @brief initial construction
     * @param serialized
     */
    void init(const QByteArray &serialized)
    {
        if (!serialized.isEmpty())
        {
            if (isPrivate())
            {
                // old method of serialize
                //                QList<QByteArray> list = Serialization::deserialize(
                //                    serialized, Serialization::DEFAULT_FIELD_SPLITTER);
                QList<QByteArray> list = Serialization::universalDesirialize(serialized, FIELDS_SIZE);

                this->id = BigNumber(list.at(0));

                QByteArray prKey = list.at(1);
                QByteArray pubKey = list.at(2);
                account = list.at(3).toInt();

                QList<QByteArray> l;
                l << prKey << pubKey;

                QByteArray keyPair = Serialization::universalSerialize(l, FIELDS_SIZE);

                this->key = new T(prKey);
            }
            else
            {
                QList<QByteArray> list = Serialization::universalDesirialize(serialized, FIELDS_SIZE);
                if (list.length() >= 2)
                {
                    this->id = BigNumber(list.at(0));
                    this->key = new T(list.at(1));
                    this->account = list.at(2).toInt();
                }
            }
            QByteArray hashData(toString().toUtf8());
            hash = Utils::calcKeccak(hashData);
        }
        else
        {
            qDebug() << "WARNING!:: Actor::init(const QByteArray &serialized) serialized IS "
                        "EMPTY!";
        }
    }
    /**
     * @brief initial construction of new Actor
     * @param id
     */
    bool initNew(const BigNumber &id, bool account)
    {
        if (isPrivate())
        {
            this->id = id;
            key = new T();
            QByteArray hashData(toString().toUtf8());
            hash = Utils::calcKeccak(hashData);
            this->account = account;
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
    void init(const BigNumber &id, const QByteArray &keydata, bool account)
    {
        this->id = id;
        this->key = new T(keydata);
        this->account = account;
    }

    bool isEmpty() const
    {
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
                QByteArray prKey = reinterpret_cast<KeyPrivate *>(key)->getPrivateKey();
                QList<QByteArray> list;
                EllipticPoints temp(hash);

                qDebug() << this->id.toString().toLocal8Bit() << prKey << pubKey;

                list << this->id.toString().toLocal8Bit() << prKey << pubKey << QByteArray::number(account);
                //                list << this->id.toString().toLocal8Bit() <<
                //                temp.CryptMessage(prKey) <<temp.CryptMessage( pubKey);
                //                KeyPublic pubKey(key->extractPublicKey());

                //                list << this->id.toString().toLocal8Bit() <<
                //                key->encrypt(hash) << pubKey.encrypt(hash);
                // old method serilaize
                //                QByteArray serialized =
                //                    Serialization::serialize(list,
                //                    Serialization::ACTOR_FIELD_SPLITTER);
                //
                QByteArray serialized = Serialization::universalSerialize(list, FIELDS_SIZE);
                return serialized;
            }
            else
            {
                // key_public
                list << id.toString().toLocal8Bit() << pubKey << QByteArray::number(account);
            }
        }
        else
        {
            list << id.toString().toLocal8Bit();
        }
        //        return Serialization::serialize(list, Serialization::ACTOR_FIELD_SPLITTER);//
        QByteArray serialized = Serialization::universalSerialize(list, 4);
        return serialized;
    }

    QString toString() const
    {
        QList<QByteArray> list;
        list << "id:" + id.toByteArray();
        if (key != nullptr)
        {
            list << "pub_key:" + key->getPublicKey();
            if (isPrivate())
            {
                list << "pr_key:" + reinterpret_cast<KeyPrivate *>(key)->getPrivateKey();
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

    bool getAccount() const
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
