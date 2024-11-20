/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <type_traits>
#include <utility>

#include <msgpack.hpp>

#include "encryption/key_private.h"
#include "encryption/key_public.h"
#include "extrachain_global.h"
#include "utils/bignumber.h"
#include "utils/exc_utils.h"

/**
 * Acting entity.
 * Users, Smart-contracts
 */

enum class ActorType {
    User       = 0,
    DAppMaster = 1,
    Service    = 2
};
MSGPACK_ADD_ENUM(ActorType)
// FORMAT_ENUM(ActorType)

class EXTRACHAIN_EXPORT ActorId {
public:
    ActorId();
    explicit ActorId(const std::string &actorId);
    ActorId(const ActorId &other);
    ActorId(ActorId &&other) noexcept;

    QByteArray toQByteArray() const;
    QString    toQString() const;

    const std::string &to_string() const;
    bool               is_zero() const;

    auto     operator<=>(const ActorId &) const = default;
    ActorId &operator=(const ActorId &actorId);
    ActorId &operator=(const std::string &actorId);
    ActorId &operator=(ActorId &&other) noexcept;

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
        if (m_id.size() > 20) {
            eFatal("[ActorId] Not correct size: %zu", m_id.size());
        }

        m_id = std::string(20 - m_id.length(), '0') + m_id;

        if (!Utils::is_hex_string_lower(m_id)) {
            eFatal("[ActorId] Not correct hex: {}", m_id.c_str());
            m_id = "00000000000000000000";
        }
    }

    std::string m_id;
};

MAKE_CUSTOM_MAGICAL(ActorId)

using TokenId = ActorId;

template <typename T>
class EXTRACHAIN_EXPORT Actor final {
    static_assert(
        (std::is_same<T, KeyPrivate>::value || std::is_same<T, KeyPublic>::value),
        "Type is not supported. Only Keys are supported");

private:
    ActorId   m_id;
    T         m_key;
    ActorType m_type = ActorType::User;

public:
    Actor()  = default;
    ~Actor() = default;

    Actor(const Actor<T> &copyActor) {
        m_id   = copyActor.id();
        m_key  = copyActor.key();
        m_type = ActorType(copyActor.type());
    }

    Actor &operator=(const Actor<T> &copyActor) {
        m_id   = copyActor.id();
        m_key  = copyActor.key();
        m_type = copyActor.type();
        return *this;
    }

public:
    /**
     * @brief initial construction of new Actor
     * @param id
     */
    void create(ActorType type) {
        static_assert(
            std::is_same<T, KeyPrivate>::value,
            "Сannot be created with a public key. Only private is supported");

        this->m_type = type;
        this->m_key.generate();
        auto        publicKey = this->m_key.publicKey();
        std::string hash      = Utils::calcHash(ByteArray(publicKey).toString(), Utils::HashEncode::Sha3_512);

        if (hash.size() >= 20)
            m_id = hash.substr(0, 20);
        else
            eFatal("[Actor] Create: error size of hash");
    }

    bool empty() const {
        if (m_key.empty())
            return true;

        return m_id.is_zero();
    }

    bool operator==(const Actor<T> &other) {
        return this->m_id == other.m_id && *m_key == *other.m_key && m_type == other.m_type;
    }

    const ActorId &id() const {
        return m_id;
    }

    const T &key() const {
        return m_key;
    }

    ActorType type() const {
        return m_type;
    }

    Actor<KeyPublic> to_public() const {
        Actor<KeyPublic> actor;

        actor.setId(m_id);
        actor.set_public_key(m_key.publicKey());
        actor.setType(m_type);

        return actor;
    }

    void setId(const ActorId &id) {
        m_id = id;
    }

    void set_secret_key(const PrivateKey &secretKey, const PublicKey &publicKey) {
        bool isPrivate = std::is_same<T, KeyPrivate>::value;
        Q_ASSERT(isPrivate);
        m_key = KeyPrivate(secretKey, publicKey);
    }

    void set_public_key(const PublicKey &key) {
        bool isPrivate = std::is_same<T, KeyPrivate>::value;
        Q_ASSERT(!isPrivate);
        m_key = KeyPublic(key);
    }

    void setType(const ActorType &type) {
        m_type = type;
    }

    QByteArray toJson() const {
        auto       array  = toJsonArray();
        QByteArray result = QJsonDocument(array).toJson(QJsonDocument::Compact);
        return result;
    }

    QJsonArray toJsonArray() const {
        if (empty()) {
            eFatal("Why actor empty?");
        }

        QJsonArray array;
        QString    pub = QString::fromStdString(Utils::to_base64(m_key.publicKey()));

        array << m_id.toQString() << int(m_type) << pub;

        if constexpr (std::is_same_v<T, KeyPrivate>) {
            QString secret = QString::fromStdString(Utils::to_base64(m_key.secretKey()));
            array << secret;
        }

        return array;
    }

    static Actor<T> fromJson(const QByteArray &serialized) {
        if (serialized.isEmpty()) {
            eFatal("[Actor] json is empty");
        }

        Actor<T> actor;
        auto     array = QJsonDocument::fromJson(serialized).array();
        actor.setId(ActorId(array[0].toString().toStdString()));
        actor.setType(ActorType(array[1].toInt()));
        auto pub = ByteArray::fromBase64(array[2].toString()).toArray<32>();

        if constexpr (std::is_same_v<T, KeyPublic>) {
            actor.set_public_key(pub);
        }
        if constexpr (std::is_same_v<T, KeyPrivate>) {
            auto sec = ByteArray::fromBase64(array[3].toString()).toArray<64>();
            actor.set_secret_key(sec, pub);
        }

        return actor;
    }

    MSGPACK_DEFINE(m_id, m_type, m_key)
    BOOST_DESCRIBE_CLASS(Actor, (), (), (), (m_id, m_type, m_key))
};
