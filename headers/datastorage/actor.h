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

#include <msgpack.hpp>

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
MSGPACK_ADD_ENUM(ActorType)

class EXTRACHAIN_EXPORT ActorId {
public:
    ActorId() {
        m_id = "00000000000000000000";
    };

    ActorId(const QByteArray &actorId) {
        if (!actorId.isEmpty() && !BigNumber::isValid(actorId))
            qFatal("ActorId not valid"); // TODO: remove after tests

        m_id = !actorId.isEmpty() ? actorId.toStdString() : "00000000000000000000";
        normalize();
    }

    ActorId(const std::string &actorId) {
        if (!actorId.empty() && !BigNumber::isValid(QByteArray::fromStdString(actorId)))
            qFatal("ActorId not valid"); // TODO: remove after tests

        m_id = !actorId.empty() ? actorId : "00000000000000000000";
        normalize();
    }

    ActorId &operator=(const QByteArray &actorId) {
        this->m_id = actorId.toStdString();
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

    QByteArray toByteArray() const {
        return QByteArray::fromStdString(m_id);
    }

    QString toString() const {
        return QString::fromStdString(m_id);
    }

    const std::string &toStdString() const {
        return m_id;
    }

    bool isEmpty() const {
        if (m_id == "000000000000000000-1")
            qFatal("ActorId: WTF");
        return m_id.empty() || m_id == "00000000000000000000";
    }

    friend QDebug operator<<(QDebug d, const ActorId &actorId) {
        d.noquote().nospace() << actorId.toByteArray();
        return d;
    }

    static bool empty(const std::string &actorId) {
        ActorId actor(actorId);
        return actor.isEmpty();
    }

private:
    void normalize() {
        m_id = QByteArray("0").repeated(20 - m_id.length()).toStdString() + m_id;
    }

    std::string m_id;
};

template <typename T>
class EXTRACHAIN_EXPORT Actor final {
    static_assert((std::is_same<T, KeyPrivate>::value || std::is_same<T, KeyPublic>::value),
                  "Your type is not supported. Only Keys are supported");

private:
    std::string m_id;
    T m_key;
    ActorType m_type;

public:
    Actor() {
        m_type = ActorType::Wallet;
    }

    Actor(const Actor<T> &copyActor) {
        m_id = copyActor.id().toStdString();
        m_key = copyActor.key();
        m_type = ActorType(copyActor.type());
    }

    Actor(const QByteArray &serialized) {
        this->deserialize(serialized);
    }

    ~Actor() = default;

    Actor operator=(const Actor<T> &copyActor) {
        m_id = copyActor.id().toStdString();
        m_key = copyActor.key();
        m_type = copyActor.type();
        return *this;
    }

public:
    /**
     * @brief initial construction
     * @param serialized
     */
    void deserialize(const QByteArray &serialized) {
        auto [actor, _] = MessagePack::deserialize<Actor<T>>(serialized.toStdString());
        *this = actor;
    }

    /**
     * @brief initial construction of new Actor
     * @param id
     */
    void create(ActorType type) {
        static_assert(std::is_same<T, KeyPrivate>::value,
                      "Сannot be created with a public key. Only private is supported");

        this->m_type = type;
        this->m_key.generate();
        auto publicKey = this->m_key.publicKey();
        auto hash = Utils::calcKeccak(QByteArray::fromStdString(publicKey));

        if (hash.size() >= 20)
            m_id = hash.left(20);
        else
            qFatal("[Actor] Create: error size of hash");
    }

    bool empty() const {
        if (m_key.empty())
            return true;

        return ActorId::empty(m_id);
    }

    /**
     * @brief serialize actor to QByteArray
     * ecdsa_private - has pubkey and prkey
     * ecdsa_public - has pubkey only
     * @return serialized actors
     */
    QByteArray serialize() const {
        std::string serialized = MessagePack::serialize(*this);
        return QByteArray::fromStdString(serialized);
    }

    PublicProfile profile() {
        QString pathToFolder = DfsStruct::ROOT_FOOLDER_NAME + "/" + ActorId(m_id).toString() + "/profile/";
        return PublicProfile(QByteArray::fromStdString(m_id), pathToFolder);
    }

public:
    bool operator==(const Actor<T> &other) {
        return this->m_id == other.m_id && *m_key == *other.m_key && m_type == other.m_type;
    }

    ActorId id() const { // TODO
        return ActorId(m_id);
    }

    const std::string &idStd() const {
        return m_id;
    }

    const T &key() const {
        return m_key;
    }

    ActorType type() const {
        return m_type;
    }

    Actor<KeyPublic> convertToPublic() {
        Actor<KeyPublic> actor;

        actor.setId(m_id);
        actor.setPublicKey(m_key.publicKey());
        actor.setType(m_type);

        return actor;
    }

    void setId(const ActorId &id) {
        m_id = id.toStdString();
    }

    void setSecretKey(const std::string &secretKey, const std::string &publicKey) {
        bool isPrivate = std::is_same<T, KeyPrivate>::value;
        Q_ASSERT(isPrivate);
        m_key = KeyPrivate(secretKey, publicKey);
    }

    void setPublicKey(const std::string &key) {
        bool isPrivate = std::is_same<T, KeyPrivate>::value;
        Q_ASSERT(!isPrivate);
        m_key = KeyPublic(key);
    }

    void setType(const ActorType &type) {
        m_type = type;
    }

    MSGPACK_DEFINE(m_id, m_type, m_key)
};

#endif // ACTOR_H
