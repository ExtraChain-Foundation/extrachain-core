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
#include <filesystem>
#include "chat/chat.h"
#include "dfs/dfs_utils.h"

class ChatManager;

class EXTRACHAIN_EXPORT ChatProfile {
public:
    explicit ChatProfile(ChatManager *owner);

    bool set_name(const std::string &name);
    bool set_bio(const std::string &bio);
    bool set_avatar(const Chat::ChatProfileAvatar &avatar);
    std::expected<Chat::ChatProfileAvatar, ChatError> upload_avatar(const std::filesystem::path &full_path,
                                                                     const std::filesystem::path &mini_path,
                                                                     const std::string           &blur_hash);

    std::expected<std::string, ChatProfileError>             read_entry(const ActorId     &chat_main_id,
                                                                         const std::string &key);
    std::expected<std::string, ChatProfileError>             read_name(const ActorId &chat_main_id);
    std::expected<std::string, ChatProfileError>             read_bio(const ActorId &chat_main_id);
    std::expected<Chat::ChatProfileAvatar, ChatProfileError> read_avatar(const ActorId &chat_main_id);

    std::expected<Dfs::DirRow, ChatError> ensure_storage_row();

private:
    std::optional<Dfs::DirRow> find_storage_row(const ActorId &chat_main_id);

    ChatManager *owner_;
};
