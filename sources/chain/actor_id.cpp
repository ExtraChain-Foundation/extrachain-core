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

#include "chain/actor_id.h"

#include <algorithm>
#include <cctype>

#include "utils/exc_logs.h"

namespace {
    bool is_lower_hex(const std::string &value) {
        return std::ranges::all_of(value, [](unsigned char character) {
            return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f');
        });
    }
} // namespace

ActorId::ActorId() {
    m_id = std::string(SIZE, '0');
}

ActorId::ActorId(const std::string &actorId) {
    m_id = actorId;
    normalize();
}

ActorId::ActorId(const ActorId &other) {
    m_id = other.m_id;
    // normalize();
}

ActorId::ActorId(ActorId &&other) noexcept {
    m_id = std::move(other.m_id);
    // normalize();
    other.m_id = std::string(SIZE, '0');
}

std::expected<ActorId, ActorError> ActorId::create(const std::string &actor_id) {
    if (actor_id.size() > SIZE) {
        return std::unexpected(ActorError::IncorrectSize);
    }

    if (!is_lower_hex(actor_id)) {
        return std::unexpected(ActorError::IncorrectFormat);
    }

    auto actor_id_result = ActorId(std::string(SIZE - actor_id.length(), '0') + actor_id);
    return actor_id_result;
}

const std::string &ActorId::value() const {
    return m_id;
}

const std::string &ActorId::to_string() const {
    return m_id;
}

bool ActorId::is_zero() const {
    return m_id == std::string(SIZE, '0') || m_id.empty();
}

ActorId &ActorId::operator=(const std::string &actorId) {
    this->m_id = actorId;
    normalize();
    return *this;
}

ActorId &ActorId::operator=(const ActorId &actorId) {
    this->m_id = actorId.m_id;
    normalize();
    return *this;
}

ActorId &ActorId::operator=(ActorId &&other) noexcept {
    if (this != &other) {
        m_id = std::move(other.m_id);
        normalize();
        other.m_id = std::string(SIZE, '0');
    }

    return *this;
}

void ActorId::normalize() {
    if (m_id.size() > SIZE) {
        eWarning("[ActorId] Incorrect size: {} / '{}'", m_id.size(), m_id);
        m_id = std::string(SIZE, '0');
        return;
    }

    m_id = std::string(SIZE - m_id.length(), '0') + m_id;

    if (!is_lower_hex(m_id)) {
        eWarning("[ActorId] Incorrect hexadecimal value: {}", m_id);
        m_id = std::string(SIZE, '0');
    }
}

namespace magic {
    std::string custom_magic<ActorId>::read(const ActorId &value) {
        return value.to_string();
    }

    ActorId custom_magic<ActorId>::write(const std::string &value) {
        return ActorId(value);
    }
} // namespace magic
