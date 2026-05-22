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
#include "chain/actor.h"
#include "chain/actor_id.h"
#include "chat/chat.h"
#include "chat/chat_folders.h"
#include "chat/message.h"
#include "dfs/dfs_utils.h"
#include "dfs/dfs_vector.h"

static const std::string CHAT_DAPP_FOLDER        = ":DApp:Chat";
static const std::string CHAT_DAPP_INVITE_FOLDER = ":DApp:Chat:Invite";

static const std::string CHAT_MY_CHATS_INFO = "MyChatsInfo";
static const std::string CHAT_PROFILE       = "ChatProfile";
static const std::string CHAT_FOLDERS       = "ChatFolders";

class ExtraChainNode;

class EXTRACHAIN_EXPORT ChatManager {
private:
    ExtraChainNode *node;

public:
    ChatManager(ExtraChainNode *node);

    void              set_mode(ChatMode mode);
    ChatMode          mode() const;
    bool              activated() const;
    // Ensures chat actor exists and scans pending invites. Idempotent.
    std::expected<void, ChatError> activate();

    std::expected<Chat::Chat, ChatError> create_chat(bool encryption = true);
    std::expected<Chat::Chat, ChatError> create_myself();
    std::expected<Chat::Chat, ChatError> create_dialogue(ActorId with);
    std::expected<Chat::Chat, ChatError> invite(const Chat::Chat &chat);

    bool set_chat_profile_name(const std::string &name);
    bool set_chat_profile_bio(const std::string &bio);
    bool set_chat_profile_avatar(const Chat::ChatProfileAvatar &avatar);
    std::expected<Chat::ChatProfileAvatar, ChatError> upload_chat_profile_avatar(
        const std::filesystem::path &full_path,
        const std::filesystem::path &mini_path,
        const std::string           &blur_hash);
    std::expected<std::string, ChatProfileError>             read_chat_profile_name(const ActorId &chat_main_id);
    std::expected<std::string, ChatProfileError>             read_chat_profile_bio(const ActorId &chat_main_id);
    std::expected<Chat::ChatProfileAvatar, ChatProfileError> read_chat_profile_avatar(const ActorId &chat_main_id);

    ChatFolders &folders() { return folders_; }

    std::expected<Chat::Chat, ChatError> create_channel(const std::string &name = "");
    std::expected<Chat::Chat, ChatError> subscribe_channel(const ActorId &owner_id, const std::string &file_id);
    std::optional<std::string>           get_channel_name(const Chat::Chat &chat);
    bool                                 set_channel_name(const Chat::Chat &chat, const std::string &name);

    std::expected<std::vector<Chat::Chat>, ChatError>    read_chats();
    std::expected<std::vector<Chat::Message>, ChatError> read_chat_messages(const ActorId     &owner_id,
                                                                            const std::string &file_id,
                                                                            bool               quick = false);
    std::expected<Chat::Message, ChatError> read_last_message(const ActorId &owner_id, const std::string &file_id);

    std::expected<Dfs::DirRow, ChatError> read_my_chats_row();

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

    std::expected<bool, ChatError> add_audio_message(const ActorId           &owner_id,
                                                     const std::string       &file_id,
                                                     const Chat::MessageText &message_text);

    std::expected<bool, ChatError> add_file_message(const ActorId           &owner_id,
                                                    const std::string       &file_id,
                                                    const Chat::MessageText &message_text);

    std::expected<bool, ChatError> edit_message(const ActorId     &owner_id,
                                                const std::string &file_id,
                                                const std::string &message_id,
                                                const std::string &new_text);

    std::expected<bool, ChatError> remove_message(const ActorId     &owner_id,
                                                  const std::string &file_id,
                                                  const std::string &message_id);

    std::optional<Chat::Chat> get_chat(const ActorId &owner_id, const std::string &file_id);

    void update_dfs_files();

private:
    std::expected<Dfs::DirRow, ChatError> create_mychats();
    std::expected<bool, ChatError>        insert_chat_to_mychats(const Chat::Chat &chat);
    std::expected<bool, ChatError>        update_chat_in_mychats(const Chat::Chat &chat);
    bool                                  parse_invite(const ActorId &owner_id, const Dfs::DirRow &dir_row);
    ActorId                               current_chat_actor_id();
    std::expected<std::reference_wrapper<const Actor<KeyPrivate>>, ChatError> current_chat_actor();

    std::expected<Dfs::DirRow, ChatError> create_chat_profile();
    std::optional<Dfs::DirRow>            find_chat_profile_row(const ActorId &chat_main_id);

    std::vector<Chat::Chat> chats_;
    ChatMode                mode_       = ChatMode::Disabled;
    bool                    activated_  = false;
    ChatFolders             folders_ { this };

    friend class ChatFolders;
};
