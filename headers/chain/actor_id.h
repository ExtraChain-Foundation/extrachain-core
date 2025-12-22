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
 * @file actor_id.h
 * @brief Defines actor-related types and classes for the chain system
 */

/**
 * @enum ActorType
 * @brief Enumeration of different types of acting entities in the system
 *
 * This enum defines the various types of actors that can interact with the chain system,
 * including users, decentralized application masters, and services.
 */
enum class ActorType {
    User       = 0, ///< Regular user entity
    DAppMaster = 1, ///< Decentralized application master contract
    Service    = 2  ///< System service entity
};
MSGPACK_ADD_ENUM(ActorType)
// FORMAT_ENUM(ActorType)

/**
 * @enum ActorError
 * @brief Error codes for actor id related operations
 *
 * Defines possible error conditions that can occur when working with actor id objects.
 */
enum class ActorError {
    IncorrectSize,  ///< The actor id exceeds the maximum allowed size (ChainConst::ACTOR_SIZE)
    IncorrectFormat ///< The actor id is not a valid lowercase hexadecimal string
};

/**
 * @class ActorId
 * @brief Represents a unique identifier for an actor in the chain system
 *
 * The ActorId class encapsulates a fixed-size hexadecimal string identifier for actors
 * (users, smart contracts, services) in the chain system. All actor ids are normalized
 * to ChainConst::ACTOR_SIZE length with leading zeros and validated as lowercase hexadecimal strings.
 *
 * @note This class is designed to be lightweight and efficiently copyable/movable.
 * @note All actor ids are stored as lowercase hexadecimal strings of fixed length.
 */
class EXTRACHAIN_EXPORT ActorId final {
public:
    /**
     * @brief Default constructor
     *
     * Creates an actor id initialized with all zeros, having ChainConst::ACTOR_SIZE length.
     */
    ActorId();

    /**
     * @brief Construct actor id from string
     * @param actorId The string representation of the actor id
     *
     * Creates an actor id from the provided string. The string will be normalized
     * to ChainConst::ACTOR_SIZE length with leading zeros and validated as lowercase hex.
     *
     * @warning This constructor may call eFatal() if the input is invalid.
     */
    explicit ActorId(const std::string &actorId);

    /**
     * @brief Copy constructor
     * @param other The actor id to copy from
     *
     * Creates a copy of another actor id without normalization since the source is already valid.
     */
    ActorId(const ActorId &other);

    /**
     * @brief Move constructor
     * @param other The actor id to move from
     *
     * Moves the content from another actor id and resets the source to zero value.
     */
    ActorId(ActorId &&other) noexcept;

    /**
     * @brief Factory method to create a validated actor id
     * @param actor_id The string representation of the actor id to validate
     * @return std::expected containing either a valid actor id or an ActorError
     *
     * This method performs validation on the input string:
     * - Checks if size does not exceed ChainConst::ACTOR_SIZE
     * - Validates that the string contains only lowercase hexadecimal characters
     * - Automatically pads with leading zeros to reach ChainConst::ACTOR_SIZE length
     *
     * @note This is the safe way to create actor ids from untrusted input.
     */
    static std::expected<ActorId, ActorError> create(const std::string actor_id);

    /**
     * @brief Convert actor id to QByteArray
     * @return QByteArray representation of the actor id
     */
    QByteArray toQByteArray() const;

    /**
     * @brief Convert actor id to QString
     * @return QString representation of the actor id
     */
    QString toQString() const;

    /**
     * @brief Get the string value of the actor id
     * @return Const reference to the internal string representation
     */
    const std::string &value() const;

    /**
     * @brief Get string representation of the actor id
     * @return Const reference to the internal string representation
     *
     * @note This method is equivalent to value() and provided for consistency
     */
    const std::string &to_string() const;

    /**
     * @brief Check if the actor id represents a zero/empty value
     * @return true if the actor id is all zeros or empty string, false otherwise
     */
    bool is_zero() const;

    /**
     * @brief Equality comparison operator
     * @param other The actor id to compare with
     * @return true if the actor ids are equal, false otherwise
     */
    bool operator==(const ActorId &) const = default;

    /**
     * @brief Less-than comparison operator for ordering
     * @param other The actor id to compare with
     * @return true if this actor id is lexicographically less than the other
     *
     * This operator enables actor id to be used in ordered containers like std::set and std::map.
     */
    bool operator<(const ActorId &other) const {
        return m_id < other.m_id;
    }

    /**
     * @brief Copy assignment operator
     * @param actorId The actor id to assign from
     * @return Reference to this actor id
     *
     * Assigns from another actor id and normalizes the result.
     */
    ActorId &operator=(const ActorId &actorId);

    /**
     * @brief String assignment operator
     * @param actorId The string to assign as the actor id
     * @return Reference to this actor id
     *
     * The assigned string will be normalized to ChainConst::ACTOR_SIZE length with leading zeros.
     *
     * @warning This operator may call eFatal() if the input is invalid.
     */
    ActorId &operator=(const std::string &actorId);

    /**
     * @brief Move assignment operator
     * @param other The actor id to move from
     * @return Reference to this actor id
     *
     * Moves the content from another actor id, normalizes the result, and resets the source to zero.
     */
    ActorId &operator=(ActorId &&other) noexcept;

    /**
     * @brief MessagePack serialization method
     * @tparam Packer The MessagePack packer type
     * @param msgpack_pk The packer instance to use for serialization
     *
     * Serializes the actor id to MessagePack format as a string.
     */
    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        msgpack_pk.pack_str(m_id.size());
        msgpack_pk.pack_str_body(m_id.data(), m_id.size());
    }

    /**
     * @brief MessagePack deserialization method
     * @param msgpack_o The MessagePack object to deserialize from
     *
     * Deserializes the actor id from MessagePack format.
     */
    void msgpack_unpack(msgpack::object const &msgpack_o) {
        m_id = msgpack_o.as<std::string>();
    }

private:
    /**
     * @brief Normalize the internal actor id string
     *
     * Applies internal normalization rules to ensure consistent formatting
     * of the actor id string.
     */
    void normalize();

    std::string m_id; ///< Internal string representation of the actor id
};

MAKE_CUSTOM_MAGICAL(ActorId)

/**
 * @typedef TokenId
 * @brief Type alias for ActorId when used as a token identifier
 *
 * In contexts where actor id represents a token rather than a general actor,
 * this alias provides semantic clarity. Tokens follow the same format as actor ids.
 */
using TokenId = ActorId;

struct NodeId {
    ActorId     actor_id;
    std::string node_identifier;

    bool operator==(const NodeId &) const = default;
    bool operator<(const NodeId &other) const;

    bool empty() {
        return actor_id.is_zero() || node_identifier.empty();
    }
};
BOOST_DESCRIBE_STRUCT(NodeId, (), (actor_id, node_identifier))

/**
 * @brief Standard library hash specialization for actor id
 *
 * This specialization allows actor id to be used as a key in unordered containers
 * like std::unordered_map and std::unordered_set.
 */
namespace std {
    template <>
    struct hash<ActorId> {
        /**
         * @brief Hash function for actor id
         * @param id The actor id to hash
         * @return Hash value for the actor id
         */
        size_t operator()(const ActorId &id) const {
            return std::hash<std::string> {}(id.value());
        }
    };

    template <>
    struct hash<NodeId> {
        size_t operator()(const NodeId &node_id) const {
            return std::hash<std::string> {}(node_id.actor_id.to_string() + node_id.node_identifier);
        }
    };
} // namespace std
