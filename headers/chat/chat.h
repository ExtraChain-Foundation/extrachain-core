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
    enum ChatType {
        Dialogue,
        Group,
        Channel,
        Bot
    };

    struct ChatData {
        std::optional<ActorId> peer_id;
        // std::optional<std::vector<ActorId>> peers_id;
        std::optional<ChatType> chat_type;
    };
    BOOST_DESCRIBE_STRUCT(ChatData, (), (peer_id, chat_type))

    struct Chat {
        std::string id;
        ChatData    chat;
        ActorId     owner_id;
        std::string file_id;
        KeyBytes    chat_key;
    };
    BOOST_DESCRIBE_STRUCT(Chat, (), (id, chat, chat_key, owner_id, file_id))
} // namespace Chat
