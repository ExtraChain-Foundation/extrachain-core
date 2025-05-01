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
// #include "encryption/encryption_tools.h"

namespace Chat {
    enum class MessageType {
        Text,    // 0
        Created, // 1
        Invite,  // 2
        Join,    // 3
        Gif,     // 4
        Image,   // 5
        Video,   // 6
        File,    // 7
    };

    class MessegeDelegateType : public QObject {
        Q_OBJECT
    public:
        enum TypeDelegate {
            Text,
            Created,
            Invite,
            Join,
            Gif,
            Image,
            Video,
            File
        };
        Q_ENUM(TypeDelegate)
    };

    struct MessageData {
        std::optional<MessageType> type;
        std::optional<std::string> data;
    };
    BOOST_DESCRIBE_STRUCT(MessageData, (), (type, data))

    struct Message {
        std::string   id;
        std::uint64_t timestamp = 0;
        ActorId       actor;
        MessageData   message;
    };
    BOOST_DESCRIBE_STRUCT(Message, (), (id, timestamp, actor, message))

    struct MessageText {
        std::string text;
    };
} // namespace Chat
