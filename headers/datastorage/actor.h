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
#include "profile/public_profile.h"

/**
 * Acting entity.
 * Users, Smart-contracts
 */

enum class ActorType
{
    Wallet = 0,
    Account = 1,
    First = 2
};

class ActorId
{
public:
    ActorId()
    {
        m_id = "00000000000000000000";
    };

    ActorId(const QByteArray &actorId)
    {
        if (!actorId.isEmpty() && !BigNumber::isValid(actorId))
            qFatal("ActorId not valid"); // TODO: remove after tests

        m_id = !actorId.isEmpty() ? actorId : QByteArray("00000000000000000000");
        normalize();
    }

    ActorId &operator=(const QByteArray &actorId)
    {
        this->m_id = actorId;
        normalize();
        return *this;
    }

    bool operator==(const ActorId &actorId) const
    {
        return m_id == actorId.m_id;
    }

    bool operator!=(const ActorId &actorId) const
    {
        return m_id != actorId.m_id;
    }

    bool operator<(const ActorId &actorId) const
    {
        return m_id < actorId.m_id;
    }

    const QByteArray &toByteArray() const
    {
        return m_id;
    }

    QString toString() const
    {
        return toByteArray();
    }

    std::string toStdString() const
    {
        return toByteArray().toStdString();
    }

    bool isEmpty() const
    {
        if (m_id == "000000000000000000-1")
            qFatal("ActorId: WTF");
        return m_id.isEmpty() || m_id == "00000000000000000000";
    }

    friend QDebug operator<<(QDebug d, const ActorId &actorId)
    {
        d.noquote().nospace() << actorId.toByteArray();
        return d;
    }

private:
    void normalize()
    {
        m_id = QByteArray("0").repeated(20 - m_id.length()) + m_id;
        // while (m_id.length() < 20)
        //     m_id.push_front('0');
    }

    QByteArray m_id;
};

template <typename T>
class Actor final
{
    static_assert((std::is_same<T, KeyPrivate>::value || std::is_same<T, KeyPublic>::value),
                  "Your type is not supported. Only Keys are supported");
    const int FIELDS_SIZE = 4;

protected:
    ActorId m_id;
    T *m_key;
    ActorType m_account;

public:
    Actor()
    {
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

        this->m_id = json["id"].toString().toLatin1();
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
            auto publicKey = k->getPubKey();
            QByteArray pk = Utils::calcKeccak(QByteArray::fromStdString(publicKey));
            if (pk.size() >= 20)
            {
                m_id = pk.left(20);
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

        return m_id.isEmpty();
    }

    /**
     * @brief serialize actor to QByteArray
     * ecdsa_private - has pubkey and prkey
     * ecdsa_public - has pubkey only
     * @return serialized actors
     */
    QByteArray serialize() const
    {
        QString actorId = QString(this->m_id.toByteArray());
        int type = static_cast<uint32_t>(m_account);

        if (m_key == nullptr || empty())
        {
            qDebug() << "Serialize empty actor";
            Q_ASSERT(!empty());
        }

        QString publicKey = QString::fromStdString(m_key->getPubKey());

        QJsonObject json = { { "id", actorId }, { "account", type }, { "publicKey", publicKey } };

        if (isPrivate())
        {
            KeyPrivate *keyPrivate = reinterpret_cast<KeyPrivate *>(m_key);
            QString privateKey = QString::fromStdString(keyPrivate->getSecKey());
            json["privateKey"] = privateKey;
        }

        QByteArray result = QJsonDocument(json).toJson(QJsonDocument::Compact);
        return result;
    }

    PublicProfile profile()
    {
        QString pathToFolder = DfsStruct::ROOT_FOOLDER_NAME + "/" + m_id.toByteArray() + "/profile/";
        return PublicProfile(m_id.toByteArray(), pathToFolder);
    }

public:
    bool operator==(const Actor<T> &other)
    {
        T *otherKey = other.key();
        return this->id() == other.id() && *m_key == *otherKey;
    }

    ActorId id() const
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

    QString accountString() const
    {
        return QString::number(int(m_account));
    }

    std::string accountStdString() const
    {
        return std::to_string(int(m_account));
    }

    Actor<KeyPublic> convertToPublic()
    {
        Actor<KeyPublic> actor;

        actor.setId(m_id);
        actor.setPublicKey(m_key->getPubKey());
        actor.setAccount(ActorType(m_account));

        return actor;
    }

    void setId(const ActorId &id)
    {
        m_id = id;
    }

    void setPublicKey(std::string key)
    {
        Q_ASSERT(!isPrivate());
        m_key = new KeyPublic(key);
    }

    void setAccount(const ActorType &account)
    {
        m_account = account;
    }
};

#endif // ACTOR_H
