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

#include "chain/actor.h"
#include "chain/actor_id.h"
#include "encryption/encryption_tools.h"

namespace Chat {
    enum ChatType {
        Dialogue,
        Group,
        Channel,
        Bot
    };

    struct ChatData {
        std::optional<ChatType> chat_type;
        std::optional<ActorId>  peer_id;
        // std::optional<std::vector<ActorId>> peers_id;
    };
    BOOST_DESCRIBE_STRUCT(ChatData, (), (peer_id, chat_type))

    struct Chat {
        std::string                       id;
        ActorId                           owner_id;
        std::string                       file_id;
        ChatData                          chat;
        std::optional<KeyBytes>           chat_key;
        std::optional<ActorId>            my_per_chat_id;
        std::optional<ActorId>            peer_chat_main_id;
        std::optional<Actor<KeyPublic>>   peer_per_chat;
        std::optional<Signature>          peer_bind_signature;
    };
    BOOST_DESCRIBE_STRUCT(Chat,
                          (),
                          (id,
                           owner_id,
                           file_id,
                           chat,
                           chat_key,
                           my_per_chat_id,
                           peer_chat_main_id,
                           peer_per_chat,
                           peer_bind_signature))

    struct ChatInvite {
        ActorId                 owner_id;
        std::string             file_id;
        std::optional<ChatType> chat_type;
        KeyBytes                chat_key;
        ActorId                 sender_chat_main_id;
        Actor<KeyPublic>        sender_per_chat;
        Signature               bind_signature;
    };
    BOOST_DESCRIBE_STRUCT(ChatInvite,
                          (),
                          (owner_id, file_id, chat_type, chat_key, sender_chat_main_id, sender_per_chat, bind_signature))

    struct MessageJoinData {
        Actor<KeyPublic> per_chat;
        Signature        bind_signature;
    };
    BOOST_DESCRIBE_STRUCT(MessageJoinData, (), (per_chat, bind_signature))
} // namespace Chat
