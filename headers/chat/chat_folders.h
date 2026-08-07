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

    // Replaces the folder's chat set; pinned ids no longer in it are dropped.
    bool set_chats(const std::string &folder_id, const std::vector<std::string> &chat_keys);
    // Chat keys that belong to a folder (empty if the folder is missing).
    std::vector<std::string> chat_ids(const std::string &folder_id);
    // Pinned chat keys of a folder (empty if the folder is missing).
    std::vector<std::string> pinned_ids(const std::string &folder_id);

    // Auto-include chat types (Chat::ChatType values). Empty list clears it.
    bool set_types(const std::string &folder_id, const std::vector<int> &types);
    std::vector<int> types(const std::string &folder_id);

    // Excluded chat keys (exceptions): override include_types and chat_ids.
    bool set_excluded(const std::string &folder_id, const std::vector<std::string> &chat_keys);
    std::vector<std::string> excluded_ids(const std::string &folder_id);

    // Membership = (include_types ∪ chat_ids) − excluded_chat_ids. chatKey = "owner:file".
    std::vector<std::string> chats_in_folder(const std::string &folder_id);

    // Persists folder ordering: each id gets order = its index in the list.
    bool set_order(const std::vector<std::string> &ordered_folder_ids);

    bool add_chat(const std::string &folder_id, const std::string &chat_key);
    bool remove_chat(const std::string &folder_id, const std::string &chat_key);

    bool pin_chat(const std::string &folder_id, const std::string &chat_key);
    bool unpin_chat(const std::string &folder_id, const std::string &chat_key);
    bool reorder_pinned(const std::string &folder_id, std::vector<std::string> ordered_keys);

private:
    std::expected<Dfs::DirRow, ChatError> ensure_storage_row();
    std::vector<Dfs::DirRow>              storage_rows();
    std::optional<Dfs::DirRow>            find_storage_row();
    bool                                  save(const Chat::ChatFolder &folder);
    Chat::ChatFolder                     *find_in_cache(const std::string &folder_id);
    bool                                  load_if_needed();

    ChatManager                  *owner_;
    std::vector<Chat::ChatFolder> cache_;
    bool                          loaded_ = false;
};
