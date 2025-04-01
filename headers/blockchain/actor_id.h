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

#include <expected>
#include <msgpack.hpp>

#include "extrachain_global.h"
#include "utils/exc_magic.h"

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

enum class ActorError {
    IncorrectSize,
    IncorrectFormat
};

class EXTRACHAIN_EXPORT ActorId final {
public:
    ActorId();
    explicit ActorId(const std::string &actorId);
    ActorId(const ActorId &other);
    ActorId(ActorId &&other) noexcept;

    static std::expected<ActorId, ActorError> create(const std::string actor_id);

    QByteArray toQByteArray() const;
    QString    toQString() const;

    const std::string &value() const;
    const std::string &to_string() const;
    bool               is_zero() const;

    bool operator==(const ActorId &) const = default;
    bool operator<(const ActorId &other) const {
        return m_id < other.m_id;
    }
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
    void normalize();

    std::string m_id;
};

MAKE_CUSTOM_MAGICAL(ActorId)

using TokenId = ActorId;

namespace std {
    template <>
    struct hash<ActorId> {
        size_t operator()(const ActorId &id) const {
            return std::hash<std::string> {}(id.value());
        }
    };
} // namespace std
