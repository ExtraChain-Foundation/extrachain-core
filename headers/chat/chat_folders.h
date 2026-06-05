/*
 * ExtraChain Core
 * Copyright (C) 2026 ExtraChain Foundation <official@extrachain.io>
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
#include "chat/chat.h"
#include "dfs/dfs_utils.h"

class ChatManager;

inline constexpr std::string_view ALL_FOLDER_ID = "all";

class EXTRACHAIN_EXPORT ChatFolders {
public:
    explicit ChatFolders(ChatManager *owner);

    const std::vector<Chat::ChatFolder> &list();
    void                                 reload();
    void                                 reset();

    std::expected<Chat::ChatFolder, ChatError> create(const std::string &name);
    bool                                       update(const Chat::ChatFolder &folder);
    bool                                       remove(const std::string &folder_id);

    bool set_name(const std::string &folder_id, const std::string &name);
    // Empty color clears the color (color -> nullopt).
    bool set_color(const std::string &folder_id, const std::string &color);

    // Replaces the whole chat set of a folder. Pinned ids that are no longer
    // part of the folder are dropped to keep chat_ids/pinned_chat_ids consistent.
    bool set_chats(const std::string &folder_id, const std::vector<std::string> &chat_keys);
    // Chat keys that belong to a folder (empty if the folder is missing).
    std::vector<std::string> chat_ids(const std::string &folder_id);

    bool add_chat(const std::string &folder_id, const std::string &chat_key);
    bool remove_chat(const std::string &folder_id, const std::string &chat_key);

    bool pin_chat(const std::string &folder_id, const std::string &chat_key);
    bool unpin_chat(const std::string &folder_id, const std::string &chat_key);
    bool reorder_pinned(const std::string &folder_id, std::vector<std::string> ordered_keys);

private:
    std::expected<Dfs::DirRow, ChatError> ensure_storage_row();
    std::optional<Dfs::DirRow>            find_storage_row();
    bool                                  save(const Chat::ChatFolder &folder);
    Chat::ChatFolder                     *find_in_cache(const std::string &folder_id);
    bool                                  load_if_needed();

    ChatManager                  *owner_;
    std::vector<Chat::ChatFolder> cache_;
    bool                          loaded_ = false;
};
