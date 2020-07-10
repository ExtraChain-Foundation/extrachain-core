/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef ACTOR_H
#define ACTOR_H

#include <QDebug>
#include <utility>
#include <type_traits>

#include "utils/bignumber.h"
#include "enc/key_private.h"
#include "enc/key_public.h"
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
class Actor final
{
    static_assert((std::is_same<T, KeyPrivate>::value || std::is_same<T, KeyPublic>::value),
                  "Your type is not supported. Only Keys are supported");
    const int FIELDS_SIZE = 4;

protected:
    BigNumber m_id = -1;
    T *m_key;
    ActorType m_account;

public:
    Actor()
    {
        m_id = 0;
        m_key = nullptr;
        m_account = ActorType::Wallet;
    }

    Actor(const Actor<T> &copyActor)
    {
        m_id = copyActor.id();
        m_key = new T(*(copyActor.key()));
        m_account = ActorType(copyActor.account());
    }

    Actor(const QByteArray &serialized)
    {
        this->deserialize(serialized);
    }

    ~Actor()
    {
        delete m_key;
    }

    Actor operator=(const Actor<T> &copyActor)
    {
        m_id = copyActor.id();
        m_key = new T(*(copyActor.key()));
        m_account = ActorType(copyActor.account());
        return *this;
    }

    bool isPrivate() const
    {
        return std::is_same<T, KeyPrivate>::value;
    }

public:
    /**
     * @brief initial construction
     * @param serialized
     */
    bool deserialize(const QByteArray &serialized)
    {
        auto json = QJsonDocument::fromJson(serialized).object();

        if (serialized.isEmpty())
        {
            qFatal("Error! Actor::init(QByteArray): serialized is empty");
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

        this->m_id = BigNumber(json["id"].toString().toLatin1());
        this->m_key = new T(json);
        this->m_account = ActorType(json["account"].toInt());

        if (empty())
        {
            qDebug() << "Incorrect actor init";
            return false;
        }

        return true;
    }

    /**
     * @brief initial construction of new Actor
     * @param id
     */
    bool create(ActorType account)
    {
        if (!isPrivate())
            return false;

        m_key = new T();

        if (typeid(T) == typeid(KeyPrivate))
        {
            KeyPrivate *k = reinterpret_cast<KeyPrivate *>(m_key);
            k->generate();
            auto publicKey = k->getPublicKey();
            QByteArray x = publicKey.x().toByteArray();
            QByteArray y = publicKey.y().toByteArray();
            QByteArray keccakPublicKey = Utils::calcKeccak(x + y);

            if (keccakPublicKey.size() >= 20)
            {
                m_id = BigNumber(keccakPublicKey.right(20));
            }
            else
            {
                qDebug() << "[Error] Actor.h func InitNew. Error size of hashPubKey";
            }
        }

        this->m_account = account;
        return true;
    }

    bool empty() const
    {
        if (m_key == nullptr)
            return true;

        if (!isPrivate())
        {
            KeyPublic *pbKey = reinterpret_cast<KeyPublic *>(m_key);
            return pbKey->isEmpty();
        }

        return m_id == BigNumber(-1);
    }

    /**
     * @brief serialize actor to QByteArray
     * ecdsa_private - has pubkey and prkey
     * ecdsa_public - has pubkey only
     * @return serialized actors
     */
    QByteArray serialize() const
    {
        QString actorId = QString(this->m_id.toActorId());
        int type = static_cast<uint32_t>(m_account);

        if (m_key == nullptr || empty())
        {
            qDebug() << "Serialize empty actor";
            Q_ASSERT(!empty());
        }

        EllipticPoint publicKey = m_key->getPublicKey();
        QString x = publicKey.x().toByteArray();
        QString y = publicKey.y().toByteArray();

        QJsonObject json = { { "id", actorId },
                             { "account", type },
                             { "publicKey", QJsonObject { { "x", x }, { "y", y } } } };

        if (isPrivate())
        {
            KeyPrivate *keyPrivate = reinterpret_cast<KeyPrivate *>(m_key);
            QString privateKey = keyPrivate->getPrivateKey().toByteArray();
            json["privateKey"] = privateKey;
        }

        QByteArray result = QJsonDocument(json).toJson(QJsonDocument::Compact);
        return result;
    }

    PublicProfile profile()
    {
        QString pathToFolder = DfsStruct::ROOT_FOOLDER_NAME + "/" + m_id.toActorId() + "/profile/";
        return PublicProfile(m_id.toActorId(), pathToFolder);
    }

public:
    bool operator==(const Actor<T> &other)
    {
        T *otherKey = other.key();
        return this->id() == other.id() && *m_key == *otherKey;
    }

    BigNumber id() const
    {
        return m_id;
    }

    T *key() const
    {
        return m_key;
    }

    ActorType account() const
    {
        return m_account;
    }

    Actor<KeyPublic> convertToPublic()
    {
        Actor<KeyPublic> actor;

        actor.setId(m_id);
        actor.setPublicKey(m_key->getPublicKey());
        actor.setAccount(ActorType(m_account));

        return actor;
    }

    void setId(const BigNumber &id)
    {
        m_id = id;
    }

    void setPublicKey(EllipticPoint point)
    {
        Q_ASSERT(!isPrivate());
        m_key = new T(point);
    }

    void setAccount(const ActorType &account)
    {
        m_account = account;
    }
};

#endif // ACTOR_H
