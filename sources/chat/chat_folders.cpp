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

#include "chat/chat_folders.h"

#include "chat/chat_manager.h"
#include "dfs/dfs_controller.h"
#include "encryption/encryption_tools.h"
#include "managers/extrachain_node.h"

ChatFolders::ChatFolders(ChatManager* owner)
    : owner_(owner) {
}

const std::vector<Chat::ChatFolder>& ChatFolders::list() {
    load_if_needed();
    return cache_;
}

void ChatFolders::reload() {
    loaded_ = false;
    cache_.clear();
    load_if_needed();
}

void ChatFolders::reset() {
    loaded_ = false;
    cache_.clear();
}

std::expected<Dfs::DirRow, ChatError> ChatFolders::ensure_storage_row() {
    auto existing = find_storage_row();
    if (existing.has_value()) {
        return existing.value();
    }
    auto chat_actor_id  = owner_->current_chat_actor_id();
    auto security_actor = Dfs::DataSecuritySelf { .my_actor = chat_actor_id };
    auto store_res      = owner_->node->dfs()->store_dictionary(chat_actor_id,
                                                           chat_actor_id,
                                                           CHAT_FOLDERS,
                                                           Dfs::DataSecurity::Self,
                                                           security_actor);
    if (!store_res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }
    return store_res.value();
}

std::optional<Dfs::DirRow> ChatFolders::find_storage_row() {
    auto chat_actor_id = owner_->current_chat_actor_id();
    if (chat_actor_id.is_zero()) {
        return std::nullopt;
    }
    auto rows =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(owner_->node->dfs()->get_db_instance(), chat_actor_id);
    if (!rows.has_value()) {
        return std::nullopt;
    }
    auto chat_actor_result = owner_->current_chat_actor();
    if (!chat_actor_result.has_value()) {
        return std::nullopt;
    }
    const auto& chat_actor = chat_actor_result->get();
    for (const auto& row : rows.value()) {
        if (row.folder != Dfs::Basic::TEMPLATE_DICTIONARY) {
            continue;
        }
        auto from_base64 = Utils::from_base64(row.name);
        if (!from_base64.has_value()) {
            continue;
        }
        auto name_result = chat_actor.key().decrypt_self(ByteArray(from_base64.value()).toBytes());
        if (!name_result.has_value()) {
            continue;
        }
        auto name = ByteArray(name_result.value()).toString();
        if (name == CHAT_FOLDERS) {
            return row;
        }
    }
    return std::nullopt;
}

bool ChatFolders::load_if_needed() {
    if (loaded_) {
        return true;
    }
    auto folder_row = find_storage_row();
    if (!folder_row.has_value()) {
        loaded_ = true;
        return true;
    }
    auto chat_actor_id  = owner_->current_chat_actor_id();
    auto security_actor = Dfs::DataSecuritySelf { .my_actor = chat_actor_id };
    auto rows = owner_->node->dfs()->read_dictionary_rows(chat_actor_id, folder_row->file_id, security_actor);
    if (!rows.has_value()) {
        loaded_ = true;
        return true;
    }
    cache_.clear();
    cache_.reserve(rows->size());
    for (const auto& [key, value] : rows.value()) {
        auto folder = Json::deserialize<Chat::ChatFolder>(value);
        if (!folder.has_value()) {
            continue;
        }
        cache_.push_back(folder.value());
    }
    std::sort(cache_.begin(), cache_.end(),
              [](const Chat::ChatFolder& a, const Chat::ChatFolder& b) { return a.order < b.order; });
    loaded_ = true;
    return true;
}

bool ChatFolders::save(const Chat::ChatFolder& folder) {
    if (folder.id.empty()) {
        return false;
    }
    auto folder_row = ensure_storage_row();
    if (!folder_row.has_value()) {
        return false;
    }
    auto chat_actor_id  = owner_->current_chat_actor_id();
    auto security_actor = Dfs::DataSecuritySelf { .my_actor = chat_actor_id };
    auto value          = Json::serialize(folder);
    auto written = owner_->node->dfs()->dictionary_set_value(chat_actor_id, folder_row->file_id, folder.id,
                                                              value, chat_actor_id, security_actor);
    if (!written) {
        return false;
    }
    if (auto* existing = find_in_cache(folder.id)) {
        *existing = folder;
    } else {
        cache_.push_back(folder);
    }
    // Always keep the cache ordered by `order`: updating an existing folder may
    // change its order (e.g. set_order), so re-sort in both branches.
    std::sort(cache_.begin(), cache_.end(),
              [](const Chat::ChatFolder& a, const Chat::ChatFolder& b) { return a.order < b.order; });
    return true;
}

Chat::ChatFolder* ChatFolders::find_in_cache(const std::string& folder_id) {
    for (auto& folder : cache_) {
        if (folder.id == folder_id) {
            return &folder;
        }
    }
    return nullptr;
}

std::expected<Chat::ChatFolder, ChatError> ChatFolders::create(const std::string& name) {
    load_if_needed();
    // New folder goes to the end: order = max(order) + 1. Without this every
    // folder would keep the default order 0 and the list order would be unstable.
    int next_order = 0;
    for (const auto& f : cache_) {
        next_order = std::max(next_order, f.order + 1);
    }
    Chat::ChatFolder folder { .id = Utils::generate_random_hex(6), .name = name, .order = next_order };
    if (!save(folder)) {
        return std::unexpected(ChatError::Unknown);
    }
    return folder;
}

bool ChatFolders::update(const Chat::ChatFolder& folder) {
    load_if_needed();
    return save(folder);
}

bool ChatFolders::remove(const std::string& folder_id) {
    load_if_needed();
    auto folder_row = find_storage_row();
    if (!folder_row.has_value()) {
        return false;
    }
    auto chat_actor_id = owner_->current_chat_actor_id();
    auto removed =
        owner_->node->dfs()->dictionary_remove_value(chat_actor_id, folder_row->file_id, folder_id, chat_actor_id);
    if (!removed) {
        return false;
    }
    std::erase_if(cache_, [&](const Chat::ChatFolder& f) { return f.id == folder_id; });
    return true;
}

bool ChatFolders::set_name(const std::string& folder_id, const std::string& name) {
    load_if_needed();
    auto* folder = find_in_cache(folder_id);
    if (!folder) {
        return false;
    }
    Chat::ChatFolder updated = *folder;
    updated.name             = name;
    return save(updated);
}

bool ChatFolders::set_color(const std::string& folder_id, const std::string& color) {
    load_if_needed();
    auto* folder = find_in_cache(folder_id);
    if (!folder) {
        return false;
    }
    Chat::ChatFolder updated = *folder;
    updated.color            = color.empty() ? std::nullopt : std::optional<std::string>(color);
    return save(updated);
}

bool ChatFolders::set_chats(const std::string& folder_id, const std::vector<std::string>& chat_keys) {
    load_if_needed();
    auto* folder = find_in_cache(folder_id);
    if (!folder) {
        return false;
    }
    Chat::ChatFolder updated = *folder;
    updated.chat_ids         = chat_keys;
    // Keep pinned_chat_ids a subset of chat_ids.
    std::erase_if(updated.pinned_chat_ids, [&](const std::string& p) {
        return std::find(updated.chat_ids.begin(), updated.chat_ids.end(), p) == updated.chat_ids.end();
    });
    return save(updated);
}

std::vector<std::string> ChatFolders::chat_ids(const std::string& folder_id) {
    load_if_needed();
    auto* folder = find_in_cache(folder_id);
    if (!folder) {
        return {};
    }
    return folder->chat_ids;
}

std::vector<std::string> ChatFolders::pinned_ids(const std::string& folder_id) {
    load_if_needed();
    auto* folder = find_in_cache(folder_id);
    if (!folder) {
        return {};
    }
    return folder->pinned_chat_ids;
}

bool ChatFolders::set_order(const std::vector<std::string>& ordered_folder_ids) {
    load_if_needed();

    // Snapshot the target order first. We must NOT hold pointers into cache_
    // across save(), because save() re-sorts cache_ and would invalidate them.
    std::vector<Chat::ChatFolder> to_write;
    for (std::size_t i = 0; i < ordered_folder_ids.size(); ++i) {
        auto* folder = find_in_cache(ordered_folder_ids[i]);
        if (!folder) {
            continue;
        }
        if (folder->order == static_cast<int>(i)) {
            continue; // already in place
        }
        Chat::ChatFolder updated = *folder;
        updated.order            = static_cast<int>(i);
        to_write.push_back(updated);
    }

    bool all_ok = true;
    for (const auto& folder : to_write) {
        if (!save(folder)) {
            all_ok = false;
        }
    }
    return all_ok;
}

bool ChatFolders::add_chat(const std::string& folder_id, const std::string& chat_key) {
    load_if_needed();
    auto* folder = find_in_cache(folder_id);
    if (!folder) {
        return false;
    }
    if (std::find(folder->chat_ids.begin(), folder->chat_ids.end(), chat_key) != folder->chat_ids.end()) {
        return true;
    }
    Chat::ChatFolder updated = *folder;
    updated.chat_ids.push_back(chat_key);
    return save(updated);
}

bool ChatFolders::remove_chat(const std::string& folder_id, const std::string& chat_key) {
    load_if_needed();
    auto* folder = find_in_cache(folder_id);
    if (!folder) {
        return false;
    }
    Chat::ChatFolder updated = *folder;
    std::erase(updated.chat_ids, chat_key);
    std::erase(updated.pinned_chat_ids, chat_key);
    return save(updated);
}

bool ChatFolders::pin_chat(const std::string& folder_id, const std::string& chat_key) {
    load_if_needed();
    auto* folder = find_in_cache(folder_id);
    if (!folder && folder_id == ALL_FOLDER_ID) {
        Chat::ChatFolder all { .id = std::string(ALL_FOLDER_ID) };
        if (!save(all)) {
            return false;
        }
        folder = find_in_cache(folder_id);
    }
    if (!folder) {
        return false;
    }
    Chat::ChatFolder updated = *folder;
    if (folder_id != ALL_FOLDER_ID
        && std::find(updated.chat_ids.begin(), updated.chat_ids.end(), chat_key) == updated.chat_ids.end()) {
        updated.chat_ids.push_back(chat_key);
    }
    if (std::find(updated.pinned_chat_ids.begin(), updated.pinned_chat_ids.end(), chat_key)
        != updated.pinned_chat_ids.end()) {
        return true;
    }
    updated.pinned_chat_ids.push_back(chat_key);
    return save(updated);
}

bool ChatFolders::unpin_chat(const std::string& folder_id, const std::string& chat_key) {
    load_if_needed();
    auto* folder = find_in_cache(folder_id);
    if (!folder) {
        return false;
    }
    Chat::ChatFolder updated = *folder;
    auto             before  = updated.pinned_chat_ids.size();
    std::erase(updated.pinned_chat_ids, chat_key);
    if (updated.pinned_chat_ids.size() == before) {
        return true;
    }
    return save(updated);
}

bool ChatFolders::reorder_pinned(const std::string& folder_id, std::vector<std::string> ordered_keys) {
    load_if_needed();
    auto* folder = find_in_cache(folder_id);
    if (!folder) {
        return false;
    }
    Chat::ChatFolder updated  = *folder;
    updated.pinned_chat_ids   = std::move(ordered_keys);
    return save(updated);
}
