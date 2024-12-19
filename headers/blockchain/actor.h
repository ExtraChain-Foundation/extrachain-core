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

#include <msgpack.hpp>

#include "blockchain/actor_id.h"
#include "encryption/key_private.h"
#include "encryption/key_public.h"
#include "extrachain_global.h"

template <typename T>
class EXTRACHAIN_EXPORT Actor final {
    static_assert((std::is_same<T, KeyPrivate>::value || std::is_same<T, KeyPublic>::value),
                  "Type is not supported. Only Keys are supported");

private:
    ActorId   id_;
    T         key_;
    ActorType type_ = ActorType::User;

public:
    Actor()  = default;
    ~Actor() = default;

    Actor(const Actor<T> &copyActor) {
        id_   = copyActor.id();
        key_  = copyActor.key();
        type_ = ActorType(copyActor.type());
    }

    Actor &operator=(const Actor<T> &copyActor) {
        id_   = copyActor.id();
        key_  = copyActor.key();
        type_ = copyActor.type();
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

        this->type_ = type;
        this->key_.generate();
        auto public_key = this->key_.public_key();
        auto hash       = Utils::calculate_hash(ByteArray(public_key).toString(), Utils::HashAlgorithm::Sha3_512);

        if (hash.size() >= BlockchainConst::ACTOR_SIZE)
            id_ = hash.substr(0, BlockchainConst::ACTOR_SIZE);
        else
            eFatal("[Actor] Create: error size of hash");
    }

    bool empty() const {
        if (key_.empty())
            return true;

        return id_.is_zero();
    }

    bool operator==(const Actor<T> &other) {
        return this->id_ == other.id_ && *key_ == *other.key_ && type_ == other.type_;
    }

    const ActorId &id() const {
        return id_;
    }

    const T &key() const {
        return key_;
    }

    ActorType type() const {
        return type_;
    }

    Actor<KeyPublic> to_public() const {
        Actor<KeyPublic> actor;

        actor.set_id(id_);
        actor.set_public_key(key_.public_key());
        actor.set_type(type_);

        return actor;
    }

    void set_id(const ActorId &id) {
        id_ = id;
    }

    void set_secret_key(const PrivateKey &secretKey, const PublicKey &publicKey) {
        bool isPrivate = std::is_same<T, KeyPrivate>::value;
        Q_ASSERT(isPrivate);
        key_ = KeyPrivate(secretKey, publicKey);
    }

    void set_public_key(const PublicKey &key) {
        bool isPrivate = std::is_same<T, KeyPrivate>::value;
        Q_ASSERT(!isPrivate);
        key_ = KeyPublic(key);
    }

    void set_type(const ActorType &type) {
        type_ = type;
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
        QString    pub = QString::fromStdString(Utils::to_base64(key_.public_key()));

        array << id_.toQString() << int(type_) << pub;

        if constexpr (std::is_same_v<T, KeyPrivate>) {
            QString secret = QString::fromStdString(Utils::to_base64(key_.secret_key()));
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
        actor.set_id(ActorId(array[0].toString().toStdString()));
        actor.set_type(ActorType(array[1].toInt()));
        auto pub = ByteArray::fromBase64(array[2].toString()).toArray<crypto_sign_PUBLICKEYBYTES>();

        if constexpr (std::is_same_v<T, KeyPublic>) {
            actor.set_public_key(pub);
        }
        if constexpr (std::is_same_v<T, KeyPrivate>) {
            auto sec = ByteArray::fromBase64(array[3].toString()).toArray<crypto_sign_SECRETKEYBYTES>();
            actor.set_secret_key(sec, pub);
        }

        return actor;
    }

    MSGPACK_DEFINE(id_, type_, key_)
    BOOST_DESCRIBE_CLASS(Actor, (), (), (), (id_, type_, key_))
};
