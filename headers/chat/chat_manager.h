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

#include <extrachain_global.h>
#include <expected>
#include "chain/actor_id.h"
#include "chat/chat.h"
#include "chat/message.h"
#include "dfs/dfs_utils.h"
#include "dfs/dfs_vector.h"

static const std::string CHAT_DAPP_FOLDER        = ":DApp:Chat";
static const std::string CHAT_DAPP_INVITE_FOLDER = ":DApp:Chat:Invite";

static const std::string CHAT_MY_CHATS = "MyChats";

class ExtraChainNode;

enum class ChatError {
    Unknown
};

class EXTRACHAIN_EXPORT ChatManager {
private:
    ExtraChainNode *node;

public:
    ChatManager(ExtraChainNode *node);

    std::expected<Chat::Chat, ChatError> create_chat(bool encryption = true);
    std::expected<Chat::Chat, ChatError> create_myself();
    std::expected<Chat::Chat, ChatError> create_dialogue(ActorId with);
    std::expected<Chat::Chat, ChatError> invite(const Chat::Chat &chat);

    std::expected<Chat::Chat, ChatError> create_channel();

    std::expected<std::vector<Chat::Chat>, ChatError>    read_chats();
    std::expected<std::vector<Chat::Message>, ChatError> read_chat_messages(const ActorId     &owner_id,
                                                                            const std::string &file_id,
                                                                            bool               quick = false);

    std::expected<Dfs::DirRow, ChatError> get_my_chats();

    std::expected<bool, ChatError> add_new_message(const ActorId       &owner_id,
                                                   const std::string   &file_id,
                                                   const Chat::Message &message);

    std::expected<bool, ChatError> add_new_message_text(const ActorId           &owner_id,
                                                        const std::string       &file_id,
                                                        const Chat::MessageText &message_text);

    std::expected<bool, ChatError> add_new_message_created(const ActorId &owner_id, const std::string &file_id);

    std::expected<bool, ChatError> add_new_message_invite(const ActorId     &owner_id,
                                                          const std::string &file_id,
                                                          const ActorId     &actor);
    std::expected<bool, ChatError> add_new_message_joined(const ActorId     &owner_id,
                                                          const std::string &file_id,
                                                          const ActorId     &actor);
    std::expected<bool, ChatError> add_gif_message(const ActorId           &owner_id,
                                                   const std::string       &file_id,
                                                   const Chat::MessageText &message_text);

    std::expected<bool, ChatError> add_image_message(const ActorId           &owner_id,
                                                     const std::string       &file_id,
                                                     const Chat::MessageText &message_text);

    std::expected<bool, ChatError> add_video_message(const ActorId           &owner_id,
                                                     const std::string       &file_id,
                                                     const Chat::MessageText &message_text);

    std::expected<bool, ChatError> add_file_message(const ActorId           &owner_id,
                                                    const std::string       &file_id,
                                                    const Chat::MessageText &message_text);

    std::expected<bool, ChatError> remove_message(const ActorId     &owner_id,
                                                  const std::string &file_id,
                                                  const std::string &message_id);

    std::optional<Chat::Chat> get_chat(const ActorId &owner_id, const std::string &file_id);

private:
    std::expected<Dfs::DirRow, ChatError> create_mychats();
    std::expected<bool, ChatError>        insert_chat_to_mychats(const Chat::Chat &chat);
    bool                                  parse_invite(const ActorId &owner_id, const Dfs::DirRow &dir_row);

    ActorId chat_actor_;

    std::vector<Chat::Chat> chats_;
};
