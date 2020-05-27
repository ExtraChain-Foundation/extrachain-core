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
namespace Trash {
static const QByteArray NullActor = "0";
};
enum class ActorType
{
    Wallet = 0,
    Account = 1,
    Company = 2
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
    ActorType account;

public:
    Actor()
    {
        id = 0;
        key = nullptr;
        hash = "";
        account = ActorType::Wallet;
    }

    Actor(const Actor<T> &copyActor)
    {
        id = copyActor.getId();
        key = new T(*(copyActor.getKey()));
        hash = copyActor.getHash();
        account = ActorType(copyActor.getAccount());
    }

    Actor(const QByteArray &serialized)
    {
        this->init(serialized);
    }

    Actor(const BigNumber &id, const EllipticPoint &publicKey, ActorType account)
    {
        this->init(id, publicKey, account);
    }

    ~Actor()
    {
        // delete key;
    }

    Actor operator=(const Actor<T> &copyActor)
    {
        id = copyActor.getId();
        key = new T(*(copyActor.getKey()));
        hash = copyActor.getHash();
        account = ActorType(copyActor.getAccount());
        return *this;
    }

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

private:
    bool isPrivate() const
    {
        return std::is_same<T, KeyPrivate>::value;
    }

    QByteArray getChecksumPubKey()
    {
        if (key == nullptr)
            return "0";

        QByteArray localPublicKey = "0";

        auto point = key->getPublicKey();
        QByteArray x = point.x().toByteArray();
        QByteArray y = point.y().toByteArray();
        localPublicKey = Serialization::universalSerialize({ x, y }, 2);

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
        auto json = QJsonDocument::fromJson(serialized).object();

        if (serialized.isEmpty())

        {
            qDebug() << "Error! Actor::init(QByteArray): serialized is empty!";
            return false;
        }

        if (isPrivate())
        {
            if (json.length() != 4)
            {
                qDebug() << "Incorrect actor init json length for private:" << json.length();
                return false;
            }
        }
        else
        {
            if (json.length() != 3)
            {
                qDebug() << "Incorrect actor init json length for public:" << json.length();
                return false;
            }
        }

        this->id = BigNumber(json["id"].toString().toLatin1());
        this->key = new T(json);
        this->account = ActorType(json["account"].toInt());

        if (isEmpty())
        {
            qDebug() << "Incorrect actor init";
            return false;
        }

        QByteArray hashData(serialize());
        hash = Utils::calcKeccak(hashData);
        return true;
    }
    /**
     * @brief initial construction of new Actor
     * @param id
     */
    bool init(ActorType account)
    {
        if (!isPrivate())
            return false;

        key = new T();

        if (typeid(T) == typeid(KeyPrivate))
        {
            KeyPrivate *k = reinterpret_cast<KeyPrivate *>(key);
            k->generate();
            auto publicKey = k->getPublicKey();
            QByteArray x = publicKey.x().toByteArray();
            QByteArray y = publicKey.y().toByteArray();
            QByteArray hashPubKey = Utils::calcKeccak(Serialization::universalSerialize({ x, y }, 2));

            if (hashPubKey.size() >= 20)
            {
                id = BigNumber(hashPubKey.mid(hashPubKey.size() - 20));
            }
            else
            {
                qDebug() << "[Error] Actor.h func InitNew. Error size of hashPubKey";
            }
        }

        this->account = account;
        QByteArray hashData(serialize());
        hash = Utils::calcKeccak(hashData);
        return true;
    }

    /**
     * @brief initial construction. Can be used to create public actor.
     * @param id
     * @param keydata - (private/public key)
     */
    bool init(const BigNumber &id, const EllipticPoint &publicKey, ActorType account)
    {
        this->id = id;
        this->key = new T(publicKey);
        this->account = ActorType(account);
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
        QString actorId = QString(this->id.toActorId());
        int type = static_cast<uint32_t>(account);

        if (key == nullptr || isEmpty())
        {
            QList<QByteArray> list = { id.toActorId() };
            QByteArray serialized = Serialization::universalSerialize(list, FIELDS_SIZE);
            return serialized;
        }

        EllipticPoint publicKey = key->getPublicKey();
        QString x = publicKey.x().toByteArray();
        QString y = publicKey.y().toByteArray();

        QJsonObject json = { { "id", actorId },
                             { "account", type },
                             { "public_key", QJsonObject { { "x", x }, { "y", y } } } };

        if (isPrivate())
        {
            KeyPrivate *keyPrivate = reinterpret_cast<KeyPrivate *>(key);
            QString privateKey = keyPrivate->getPrivateKey().toByteArray();
            json["private_key"] = privateKey;
        }

        QByteArray result = QJsonDocument(json).toJson(QJsonDocument::Compact);
        return result;
    }

    PublicProfile profile()
    {
        QString pathToFolder = DfsStruct::ROOT_FOOLDER_NAME + "/" + id.toActorId() + "/profile/";
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

    ActorType getAccount() const
    {
        return account;
    }

    void setAccount(ActorType value)
    {
        account = value;
    }

    Actor<KeyPublic> convertToPublic() const
    {
        //
        return isPrivate() ? Actor<KeyPublic>(getId(), getKey()->getPublicKey(), getAccount())
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
