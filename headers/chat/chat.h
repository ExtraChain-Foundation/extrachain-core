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

enum class ChatError {
    Unknown,
    Disabled,
    NoChatActor,
    NotAllowed,
    NoChannelsVector,
    InvalidPeer,
    StorageUnavailable,
    PersistenceFailed
};

enum class ChatProfileError {
    NoProfile,
    NoEntry,
    Invalid
};

enum class ChatMode {
    Disabled,
    Enabled
};

namespace Chat {
    enum ChatType {
        Dialogue,
        Group,
        Channel,
        Bot
    };

    enum class SyncState {
        LoadingHistory,
        Joining,
        Ready,
        Error
    };

    struct ChatData {
        std::optional<ChatType> chat_type;
        std::optional<ActorId>  peer_id;
        // std::optional<std::vector<ActorId>> peers_id; // TODO: group chats
    };
    BOOST_DESCRIBE_STRUCT(ChatData, (), (peer_id, chat_type))

    struct Chat {
        std::string                     id;
        ActorId                         owner_id;
        std::string                     file_id;
        ChatData                        chat;
        std::optional<KeyBytes>         chat_key;
        std::optional<ActorId>          my_per_chat_id;
        std::optional<ActorId>          peer_chat_main_id;
        std::optional<Actor<KeyPublic>> peer_per_chat;
        std::optional<Signature>        peer_bind_signature;
        bool                            invite_pending = false;
        std::optional<std::string>      invite_file_id;
        std::optional<SyncState>        sync_state;
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
                           peer_bind_signature,
                           invite_pending,
                           invite_file_id,
                           sync_state))

    struct SendResult {
        std::string message_id;
        bool        stored = false;
    };
    BOOST_DESCRIBE_STRUCT(SendResult, (), (message_id, stored))

    struct ChatInvite {
        ActorId                 owner_id;
        std::string             file_id;
        std::optional<ChatType> chat_type;
        KeyBytes                chat_key;
        ActorId                 sender_chat_main_id;
        Actor<KeyPublic>        sender_per_chat;
        Signature               bind_signature;
    };
    BOOST_DESCRIBE_STRUCT(
        ChatInvite,
        (),
        (owner_id, file_id, chat_type, chat_key, sender_chat_main_id, sender_per_chat, bind_signature))

    struct MessageJoinData {
        Actor<KeyPublic> per_chat;
        Signature        bind_signature;
    };
    BOOST_DESCRIBE_STRUCT(MessageJoinData, (), (per_chat, bind_signature))

    struct ChannelInfo {
        ActorId     owner_id;
        std::string file_id;
        std::string name;
    };
    BOOST_DESCRIBE_STRUCT(ChannelInfo, (), (owner_id, file_id, name))

    // UI prepares full (~1024px) and mini (~192px) images.
    struct ChatProfileAvatar {
        std::string full_id;
        std::string mini_id;
        std::string blur_hash;
    };
    BOOST_DESCRIBE_STRUCT(ChatProfileAvatar, (), (full_id, mini_id, blur_hash))

    struct ChatFolder {
        std::string                          id;
        std::string                          name;
        std::optional<std::string>           emoji;
        std::vector<std::string>             chat_ids;
        std::vector<std::string>             pinned_chat_ids;
        std::optional<std::vector<ActorId>>  include_chat_main_ids;
        std::optional<std::vector<ChatType>> include_types;
        // Chat keys excluded from the folder; override include_types and chat_ids.
        std::vector<std::string>   excluded_chat_ids;
        std::optional<bool>        unread_only;
        std::optional<bool>        muted;
        std::optional<std::string> color;
        int                        order = 0;
    };
    BOOST_DESCRIBE_STRUCT(ChatFolder,
                          (),
                          (id,
                           name,
                           emoji,
                           chat_ids,
                           pinned_chat_ids,
                           include_chat_main_ids,
                           include_types,
                           excluded_chat_ids,
                           unread_only,
                           muted,
                           color,
                           order))
} // namespace Chat
