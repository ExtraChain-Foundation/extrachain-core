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

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

#include <boost/json.hpp>
#include <msgpack.hpp>

#include "chain/actor_id.h"
#include "core/byte_array.h"
#include "encryption/key_private.h"
#include "encryption/key_public.h"
#include "extrachain_global.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils_base64.h"
#include "utils/hash.h"

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
                      "Cannot be created with a public key. Only private is supported");

        this->type_ = type;
        this->key_.generate_random();
        auto public_key = this->key_.public_key();
        auto hash       = Utils::calculate_hash(ByteArray(public_key).toString(), Utils::HashAlgorithm::Blake3);

        if (hash.size() >= ActorId::SIZE)
            id_ = hash.substr(0, ActorId::SIZE);
        else
            eFatal("[Actor] Create: error size of hash");
    }

    void generate_from_seed(const MasterSeed &seed, int index, ActorType type) {
        static_assert(std::is_same<T, KeyPrivate>::value,
                      "Cannot be created with a public key. Only private is supported");

        this->type_ = type;
        this->key_.generate_seed(seed, index);
        auto public_key = this->key_.public_key();
        auto hash       = Utils::calculate_hash(ByteArray(public_key).toString(), Utils::HashAlgorithm::Blake3);

        if (hash.size() >= ActorId::SIZE)
            id_ = hash.substr(0, ActorId::SIZE);
        else
            eFatal("[Actor] Create: error size of hash");
    }

    void generate_from_seed(const MasterSeed &seed, const std::string &label, ActorType type) {
        static_assert(std::is_same<T, KeyPrivate>::value,
                      "Cannot be created with a public key. Only private is supported");

        this->type_ = type;
        this->key_.generate_seed(seed, label);
        auto public_key = this->key_.public_key();
        auto hash       = Utils::calculate_hash(ByteArray(public_key).toString(), Utils::HashAlgorithm::Blake3);

        if (hash.size() >= ActorId::SIZE)
            id_ = hash.substr(0, ActorId::SIZE);
        else
            eFatal("[Actor] Create: error size of hash");
    }

    bool empty() const {
        if (key_.empty())
            return true;

        return id_.is_zero();
    }

    bool operator==(const Actor<T> &other) const {
        if (id_ != other.id_ || type_ != other.type_ || key_.public_key() != other.key_.public_key()) {
            return false;
        }
        if constexpr (std::is_same_v<T, KeyPrivate>) {
            return key_.secret_key() == other.key_.secret_key();
        }
        return true;
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
        static_assert(std::is_same_v<T, KeyPrivate>, "A secret key requires Actor<KeyPrivate>");
        key_ = KeyPrivate(secretKey, publicKey);
    }

    void set_public_key(const PublicKey &key) {
        static_assert(std::is_same_v<T, KeyPublic>, "A public key requires Actor<KeyPublic>");
        key_ = KeyPublic(key);
    }

    void set_type(const ActorType &type) {
        type_ = type;
    }

    [[nodiscard]] std::string toJson() const {
        if (empty()) {
            eWarning("[Actor] Cannot serialize an empty actor");
            return {};
        }

        boost::json::array array;
        array.emplace_back(id_.to_string());
        array.emplace_back(static_cast<std::int64_t>(type_));
        array.emplace_back(Utils::to_base64(key_.public_key()));

        if constexpr (std::is_same_v<T, KeyPrivate>) {
            array.emplace_back(Utils::to_base64(key_.secret_key()));
        }

        return boost::json::serialize(array);
    }

    [[nodiscard]] static Actor<T> fromJson(std::string_view serialized) {
        if (serialized.empty()) {
            eWarning("[Actor] JSON is empty");
            return {};
        }

        boost::system::error_code error;
        const auto                value = boost::json::parse(serialized, error);
        if (error) {
            eWarning("[Actor] Invalid JSON: {}", error.message());
            return {};
        }
        if (!value.is_array()) {
            eWarning("[Actor] JSON root is not an array");
            return {};
        }

        const auto &array         = value.as_array();
        const auto  expected_size = std::is_same_v<T, KeyPrivate> ? 4u : 3u;
        if (array.size() != expected_size || !array[0].is_string() || !array[1].is_int64()
            || !array[2].is_string()) {
            eWarning("[Actor] Invalid JSON fields");
            return {};
        }
        if constexpr (std::is_same_v<T, KeyPrivate>) {
            if (!array[3].is_string()) {
                eWarning("[Actor] Invalid private key field");
                return {};
            }
        }

        const auto id = ActorId::create(std::string(array[0].as_string()));
        if (!id.has_value() || id->is_zero()) {
            eWarning("[Actor] Invalid actor ID");
            return {};
        }

        const auto actor_type = array[1].as_int64();
        if (actor_type < static_cast<std::int64_t>(ActorType::User)
            || actor_type > static_cast<std::int64_t>(ActorType::Service)) {
            eWarning("[Actor] Invalid actor type: {}", actor_type);
            return {};
        }

        const auto public_key = ByteArray::fromBase64(std::string(array[2].as_string()));
        if (!public_key.has_value() || public_key->size() != crypto_sign_PUBLICKEYBYTES) {
            eWarning("[Actor] Invalid public key");
            return {};
        }

        Actor<T> actor;
        actor.set_id(*id);
        actor.set_type(static_cast<ActorType>(actor_type));
        const auto public_key_value = public_key->template toArray<crypto_sign_PUBLICKEYBYTES>();

        if constexpr (std::is_same_v<T, KeyPublic>) {
            actor.set_public_key(public_key_value);
        } else {
            const auto private_key = ByteArray::fromBase64(std::string(array[3].as_string()));
            if (!private_key.has_value() || private_key->size() != crypto_sign_SECRETKEYBYTES) {
                eWarning("[Actor] Invalid private key");
                return {};
            }
            actor.set_secret_key(private_key->template toArray<crypto_sign_SECRETKEYBYTES>(), public_key_value);
        }

        return actor;
    }

    MSGPACK_DEFINE(id_, type_, key_)
    BOOST_DESCRIBE_CLASS(Actor, (), (), (), (id_, type_, key_))
};
