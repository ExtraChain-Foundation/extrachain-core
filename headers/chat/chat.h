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

#include "blockchain/actor_id.h"
#include "encryption/encryption_tools.h"

namespace Chat {
    struct Chat {
        std::string            chat_id;
        ActorId                myself;
        std::optional<ActorId> another;
        KeyBytes               chat_key;

        ActorId     file_actor_id;
        std::string file_id;
    };
    BOOST_DESCRIBE_STRUCT(Chat, (), (chat_id, myself, another, chat_key, file_actor_id, file_id))
} // namespace Chat
