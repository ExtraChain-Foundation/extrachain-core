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

#include "chat/chat_profile.h"

#include "chat/chat_manager.h"
#include "dfs/dfs_controller.h"
#include "managers/extrachain_node.h"

ChatProfile::ChatProfile(ChatManager* owner)
    : owner_(owner) {
}

std::expected<Dfs::DirRow, ChatError> ChatProfile::ensure_storage_row() {
    auto existing = find_storage_row(owner_->current_chat_actor_id());
    if (existing.has_value()) {
        return existing.value();
    }
    auto chat_actor_id = owner_->current_chat_actor_id();
    auto store_res     = owner_->node->dfs()->store_dictionary(chat_actor_id, chat_actor_id, CHAT_PROFILE);
    if (!store_res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }
    return store_res.value();
}

std::optional<Dfs::DirRow> ChatProfile::find_storage_row(const ActorId& chat_main_id) {
    if (chat_main_id.is_zero()) {
        return std::nullopt;
    }
    auto row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(owner_->node->dfs()->get_db_instance(),
                                                                chat_main_id,
                                                                CHAT_PROFILE,
                                                                "name");
    if (!row.has_value()) {
        return std::nullopt;
    }
    return row.value();
}

bool ChatProfile::set_name(const std::string& name) {
    auto profile_row = ensure_storage_row();
    if (!profile_row.has_value()) {
        return false;
    }
    auto chat_actor_id = owner_->current_chat_actor_id();
    return owner_->node->dfs()->dictionary_set_value(chat_actor_id, profile_row->file_id, "name", name,
                                                      chat_actor_id);
}

bool ChatProfile::set_bio(const std::string& bio) {
    auto profile_row = ensure_storage_row();
    if (!profile_row.has_value()) {
        return false;
    }
    auto chat_actor_id = owner_->current_chat_actor_id();
    return owner_->node->dfs()->dictionary_set_value(chat_actor_id, profile_row->file_id, "bio", bio,
                                                      chat_actor_id);
}

bool ChatProfile::set_avatar(const Chat::ChatProfileAvatar& avatar) {
    auto profile_row = ensure_storage_row();
    if (!profile_row.has_value()) {
        return false;
    }
    auto chat_actor_id = owner_->current_chat_actor_id();
    auto json          = Json::serialize(avatar);
    return owner_->node->dfs()->dictionary_set_value(chat_actor_id, profile_row->file_id, "avatar", json,
                                                      chat_actor_id);
}

std::expected<Chat::ChatProfileAvatar, ChatError> ChatProfile::upload_avatar(
    const std::filesystem::path& full_path,
    const std::filesystem::path& mini_path,
    const std::string&           blur_hash) {
    auto chat_actor_id = owner_->current_chat_actor_id();

    auto full_name = fmt::format("avatar-{}", Utils::generate_random_hex(8));
    auto mini_name = fmt::format("avatar-mini-{}", Utils::generate_random_hex(8));

    auto full_res = owner_->node->dfs()->store_file(chat_actor_id, chat_actor_id, full_path, CHAT_DAPP_FOLDER,
                                                     full_name, Dfs::DataSecurity::Public);
    if (!full_res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    auto mini_res = owner_->node->dfs()->store_file(chat_actor_id, chat_actor_id, mini_path, CHAT_DAPP_FOLDER,
                                                     mini_name, Dfs::DataSecurity::Public);
    if (!mini_res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }

    Chat::ChatProfileAvatar avatar { .full_id   = full_res->file_id,
                                     .mini_id   = mini_res->file_id,
                                     .blur_hash = blur_hash };
    if (!set_avatar(avatar)) {
        return std::unexpected(ChatError::Unknown);
    }
    return avatar;
}

std::expected<std::string, ChatProfileError> ChatProfile::read_name(const ActorId& chat_main_id) {
    auto row = find_storage_row(chat_main_id);
    if (!row.has_value()) {
        return std::unexpected(ChatProfileError::NoProfile);
    }
    auto value = owner_->node->dfs()->read_dictionary(chat_main_id, row->file_id, "name");
    if (!value.has_value()) {
        return std::unexpected(ChatProfileError::NoEntry);
    }
    return value.value();
}

std::expected<std::string, ChatProfileError> ChatProfile::read_bio(const ActorId& chat_main_id) {
    auto row = find_storage_row(chat_main_id);
    if (!row.has_value()) {
        return std::unexpected(ChatProfileError::NoProfile);
    }
    auto value = owner_->node->dfs()->read_dictionary(chat_main_id, row->file_id, "bio");
    if (!value.has_value()) {
        return std::unexpected(ChatProfileError::NoEntry);
    }
    return value.value();
}

std::expected<Chat::ChatProfileAvatar, ChatProfileError> ChatProfile::read_avatar(const ActorId& chat_main_id) {
    auto row = find_storage_row(chat_main_id);
    if (!row.has_value()) {
        return std::unexpected(ChatProfileError::NoProfile);
    }
    auto value = owner_->node->dfs()->read_dictionary(chat_main_id, row->file_id, "avatar");
    if (!value.has_value()) {
        return std::unexpected(ChatProfileError::NoEntry);
    }
    auto avatar = Json::deserialize<Chat::ChatProfileAvatar>(value.value());
    if (!avatar.has_value()) {
        return std::unexpected(ChatProfileError::Invalid);
    }
    return avatar.value();
}
