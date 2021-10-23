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
#include <QJsonDocument>
#include <QJsonObject>
#include <type_traits>
#include <utility>

#include "dfs/types/headers/dfstruct.h"
#include "enc/key_private.h"
#include "enc/key_public.h"
#include "extrachain_global.h"
#include "profile/public_profile.h"
#include "utils/bignumber.h"

/**
 * Acting entity.
 * Users, Smart-contracts
 */

enum class ActorType
{
    Wallet = 0,
    Account = 1,
    Token = 2
};

class EXTRACHAIN_EXPORT ActorId {
public:
    ActorId() {
        m_id = "00000000000000000000";
    };

    ActorId(const QByteArray &actorId) {
        if (!actorId.isEmpty() && !BigNumber::isValid(actorId))
            qFatal("ActorId not valid"); // TODO: remove after tests

        m_id = !actorId.isEmpty() ? actorId : QByteArray("00000000000000000000");
        normalize();
    }

    ActorId &operator=(const QByteArray &actorId) {
        this->m_id = actorId;
        normalize();
        return *this;
    }

    bool operator==(const ActorId &actorId) const {
        return m_id == actorId.m_id;
    }

    bool operator!=(const ActorId &actorId) const {
        return m_id != actorId.m_id;
    }

    bool operator<(const ActorId &actorId) const {
        return m_id < actorId.m_id;
    }

    const QByteArray &toByteArray() const {
        return m_id;
    }

    QString toString() const {
        return toByteArray();
    }

    std::string toStdString() const {
        return toByteArray().toStdString();
    }

    bool isEmpty() const {
        if (m_id == "000000000000000000-1")
            qFatal("ActorId: WTF");
        return m_id.isEmpty() || m_id == "00000000000000000000";
    }

    friend QDebug operator<<(QDebug d, const ActorId &actorId) {
        d.noquote().nospace() << actorId.toByteArray();
        return d;
    }

private:
    void normalize() {
        m_id = QByteArray("0").repeated(20 - m_id.length()) + m_id;
        // while (m_id.length() < 20)
        //     m_id.push_front('0');
    }

    QByteArray m_id;
};

template <typename T>
class EXTRACHAIN_EXPORT Actor final {
    static_assert((std::is_same<T, KeyPrivate>::value || std::is_same<T, KeyPublic>::value),
                  "Your type is not supported. Only Keys are supported");
    const int FIELDS_SIZE = 4;

private:
    ActorId m_id;
    T *m_key;
    ActorType m_type;

public:
    Actor() {
        m_key = nullptr;
        m_type = ActorType::Wallet;
    }

    Actor(const Actor<T> &copyActor) {
        m_id = copyActor.id();
        m_key = new T(*(copyActor.key()));
        m_type = ActorType(copyActor.type());
    }

    Actor(const QByteArray &serialized) {
        this->deserialize(serialized);
    }

    ~Actor() {
        delete m_key;
    }

    Actor operator=(const Actor<T> &copyActor) {
        m_id = copyActor.id();
        m_key = new T(*(copyActor.key()));
        m_type = ActorType(copyActor.type());
        return *this;
    }

    bool isPrivate() const {
        return std::is_same<T, KeyPrivate>::value;
    }

public:
    /**
     * @brief initial construction
     * @param serialized
     */
    bool deserialize(const QByteArray &serialized) {
        auto json = QJsonDocument::fromJson(serialized).object();

        if (serialized.isEmpty()) {
            qFatal("Error! Actor::init(QByteArray): serialized is empty");
        }

        if (isPrivate()) {
            if (json.length() != 4) {
                qDebug() << "Incorrect actor init json length for private:" << json.length();
                return false;
            }
        } else {
            if (json.length() != 3) {
                qDebug() << "Incorrect actor init json length for public:" << json.length();
                return false;
            }
        }

        this->m_id = json["id"].toString().toLatin1();
        this->m_key = new T(json);
        if (json.contains("type"))
            this->m_type = ActorType(json["type"].toInt());
        else // TODO: remove
            this->m_type = ActorType(json["account"].toInt());

        if (empty()) {
            qDebug() << "Incorrect actor init";
            return false;
        }

        return true;
    }

    /**
     * @brief initial construction of new Actor
     * @param id
     */
    void create(ActorType type) {
        static_assert(std::is_same<T, KeyPrivate>::value,
                      "Сannot be created with a public key. Only private is supported");

        this->m_key = new T();
        this->m_type = type;

        auto publicKey = this->m_key->publicKey();
        auto hash = Utils::calcKeccak(QByteArray::fromStdString(publicKey));

        if (hash.size() >= 20)
            m_id = hash.left(20);
        else
            qFatal("[Actor] Create: error size of hash");
    }

    bool empty() const {
        if (m_key == nullptr)
            return true;

        if (!isPrivate()) {
            KeyPublic *pbKey = reinterpret_cast<KeyPublic *>(m_key);
            return pbKey->publicKey().empty();
        }

        return m_id.isEmpty();
    }

    /**
     * @brief serialize actor to QByteArray
     * ecdsa_private - has pubkey and prkey
     * ecdsa_public - has pubkey only
     * @return serialized actors
     */
    QByteArray serialize() const {
        QString actorId = QString(this->m_id.toByteArray());
        int type = static_cast<uint32_t>(m_type);

        if (m_key == nullptr || empty()) {
            qDebug() << "Serialize empty actor";
            Q_ASSERT(!empty());
        }

        QString publicKey = QString::fromStdString(m_key->publicKey());

        QJsonObject json = { { "id", actorId }, { "type", type }, { "publicKey", publicKey } };

        if (isPrivate()) {
            KeyPrivate *keyPrivate = reinterpret_cast<KeyPrivate *>(m_key);
            QString privateKey = QString::fromStdString(keyPrivate->secretKey());
            json["privateKey"] = privateKey;
        }

        QByteArray result = QJsonDocument(json).toJson(QJsonDocument::Compact);
        return result;
    }

    PublicProfile profile() {
        QString pathToFolder = DfsStruct::ROOT_FOOLDER_NAME + "/" + m_id.toByteArray() + "/profile/";
        return PublicProfile(m_id.toByteArray(), pathToFolder);
    }

public:
    bool operator==(const Actor<T> &other) {
        return this->m_id == other.m_id && *m_key == *other.m_key && m_type == other.m_type;
    }

    const ActorId &id() const {
        return m_id;
    }

    T *key() const {
        return m_key;
    }

    ActorType type() const {
        return m_type;
    }

    Actor<KeyPublic> convertToPublic() {
        Actor<KeyPublic> actor;

        actor.setId(m_id);
        actor.setPublicKey(m_key->publicKey());
        actor.setType(m_type);

        return actor;
    }

    void setId(const ActorId &id) {
        m_id = id;
    }

    void setSecretKey(const std::string &secretKey, const std::string &publicKey) {
        Q_ASSERT(isPrivate());
        m_key = new KeyPrivate(secretKey, publicKey);
    }

    void setPublicKey(const std::string &key) {
        Q_ASSERT(!isPrivate());
        m_key = new KeyPublic(key);
    }

    void setType(const ActorType &type) {
        m_type = type;
    }
};

#endif // ACTOR_H
