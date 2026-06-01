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

#include "chain/actor_id.h"
// #include "encryption/encryption_tools.h"

namespace Chat {
    enum class MessageType {
        Text,    // 0
        Created, // 1
        Invite,  // 2
        Join,    // 3
        Image,   // 4
        Gif,     // 5
        Audio,   // 6
        Voice,   // 7
        Video,   // 8
        File,    // 9
    };

    struct MessageData {
        std::optional<MessageType>  type;
        std::optional<std::string>  data, reply_id;
        std::optional<std::uint64_t> original_timestamp;
        std::optional<bool>          deleted_for_me;
    };
    BOOST_DESCRIBE_STRUCT(MessageData, (), (type, data, reply_id, original_timestamp, deleted_for_me))

    struct Message {
        std::string   id;
        std::uint64_t timestamp = 0;
        ActorId       actor;
        MessageData   message;
    };
    BOOST_DESCRIBE_STRUCT(Message, (), (id, timestamp, actor, message))

    struct MessageText {
        std::string text;
        std::string reply_id;
    };
} // namespace Chat
