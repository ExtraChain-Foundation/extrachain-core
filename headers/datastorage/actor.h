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
#include <QJsonArray>
#include <QJsonDocument>
#include <type_traits>
#include <utility>

#include <msgpack.hpp>

#include "enc/key_private.h"
#include "enc/key_public.h"
#include "extrachain_global.h"
#include "utils/bignumber.h"
#include "utils/exc_utils.h"

/**
 * Acting entity.
 * Users, Smart-contracts
 */

enum class ActorType {
    User = 0,
    ServiceProvider = 1,
    Service = 2
};
MSGPACK_ADD_ENUM(ActorType)
FORMAT_ENUM(ActorType)

class EXTRACHAIN_EXPORT ActorId {
public:
    ActorId() {
        m_id = "00000000000000000000";
    };

    ActorId(const std::string &actorId) {
        m_id = !actorId.empty() ? actorId : "00000000000000000000";
        normalize();
    }

    ActorId &operator=(const std::string &actorId) {
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

    friend std::ostream &operator<<(std::ostream &os, const ActorId &actorId) {
        os << actorId.toStdString();
        return os;
    }

    static bool empty(const std::string &actorId) {
        ActorId actor(actorId);
        return actor.isEmpty();
    }

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        msgpack_pk.pack_str(m_id.size());
        msgpack_pk.pack_str_body(m_id.data(), m_id.size());
    }

    void msgpack_unpack(msgpack::object const &msgpack_o) {
        m_id = msgpack_o.as<std::string>();
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
                  "Type is not supported. Only Keys are supported");

private:
    ActorId m_id;
    T m_key;
    ActorType m_type = ActorType::User;

public:
    Actor() = default;
    ~Actor() = default;

    Actor(const Actor<T> &copyActor) {
        m_id = copyActor.id();
        m_key = copyActor.key();
        m_type = ActorType(copyActor.type());
    }

    Actor &operator=(const Actor<T> &copyActor) {
        m_id = copyActor.id();
        m_key = copyActor.key();
        m_type = copyActor.type();
        return *this;
    }

public:
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
        std::string hash = Utils::calcHash(publicKey, Utils::HashEncode::Sha3_512);

        if (hash.size() >= 20)
            m_id = hash.substr(0, 20);
        else
            qFatal("[Actor] Create: error size of hash");
    }

    bool empty() const {
        if (m_key.empty())
            return true;

        return m_id.isEmpty();
    }

public:
    bool operator==(const Actor<T> &other) {
        return this->m_id == other.m_id && *m_key == *other.m_key && m_type == other.m_type;
    }

    const ActorId &id() const {
        return m_id;
    }

    [[deprecated("Use id().toStdString() instead.")]] const std::string &idStd() const {
        return m_id.toStdString();
    }

    const T &key() const {
        return m_key;
    }

    ActorType type() const {
        return m_type;
    }

    Actor<KeyPublic> convertToPublic() const {
        Actor<KeyPublic> actor;

        actor.setId(m_id);
        actor.setPublicKey(m_key.publicKey());
        actor.setType(m_type);

        return actor;
    }

    void setId(const ActorId &id) {
        m_id = id;
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

    QByteArray toJson() const {
        auto array = toJsonArray();
        QByteArray result = QJsonDocument(array).toJson(QJsonDocument::Compact);
        return result;
    }

    QJsonArray toJsonArray() const {
        if (empty()) {
            qFatal("Why actor empty?");
        }

        QJsonArray array;
        auto pub = QString(Utils::bytesEncode(QByteArray::fromStdString(m_key.publicKey())));
        array << m_id.toString() << int(m_type) << pub;

        if constexpr (std::is_same_v<T, KeyPrivate>) {
            auto secret = QString(Utils::bytesEncode(QByteArray::fromStdString(m_key.secretKey())));
            array << secret;
        }

        return array;
    }

    static Actor<T> fromJson(const QByteArray &serialized) {
        if (serialized.isEmpty()) {
            qFatal("[Actor] json is empty");
        }

        Actor<T> actor;
        auto array = QJsonDocument::fromJson(serialized).array();
        actor.setId(array[0].toString().toStdString());
        actor.setType(ActorType(array[1].toInt()));
        auto pub = Utils::bytesDecode(array[2].toString().toLatin1());

        if constexpr (std::is_same_v<T, KeyPublic>) {
            actor.setPublicKey(pub.toStdString());
        }
        if constexpr (std::is_same_v<T, KeyPrivate>) {
            auto sec = Utils::bytesDecode(array[3].toString().toLatin1());
            actor.setSecretKey(sec.toStdString(), pub.toStdString());
        }

        return actor;
    }

    friend QDebug operator<<(QDebug d, const Actor<T> &actor) {
        QDebugStateSaver saver(d);
        d << "Actor { id:" << actor.id() << ", type: " << magic_enum::enum_name(actor.type()).data()
          << ", key: " << actor.key() << "}";
        return d;
    }

    friend std::ostream &operator<<(std::ostream &os, const Actor<T> &actor) {
        os << "Actor { id: " << actor.id() << ", type: " << actor.type() << ", key: " << actor.key() << " }";
        return os;
    }

    MSGPACK_DEFINE(m_id, m_type, m_key)
};

#endif // ACTOR_H
