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
#include "dfs/dfs_service.h"
#include "core/extrachain_node.h"

ChatProfile::ChatProfile(ChatManager* owner)
    : owner_(owner) {
}

std::expected<Dfs::DirRow, ChatError> ChatProfile::ensure_storage_row() {
    auto existing = find_storage_row(owner_->current_chat_actor_id());
    if (existing.has_value()) {
        return existing.value();
    }
    auto chat_actor_id = owner_->current_chat_actor_id();
    if (chat_actor_id.is_zero()) {
        return std::unexpected(ChatError::NoChatActor);
    }
    auto store_res = owner_->node->dfs()->store_dictionary(chat_actor_id, chat_actor_id, CHAT_PROFILE);
    if (!store_res.has_value()) {
        return std::unexpected(ChatError::Unknown);
    }
    return store_res.value();
}

// All ChatProfile copies, best-first: profiles got fragmented across duplicates,
// so reads must fall through older copies for missing keys
static std::vector<Dfs::DirRow> profile_storage_rows(ExtraChain::Core::ExtraChainNode* node,
                                                     const ActorId&                    chat_main_id) {
    std::vector<Dfs::DirRow> out;
    if (chat_main_id.is_zero()) {
        return out;
    }
    auto rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(node->dfs()->get_db_instance(),
                                                                chat_main_id);
    if (!rows.has_value()) {
        return out;
    }
    for (const auto& row : rows.value()) {
        if (row.name == CHAT_PROFILE && row.state != Dfs::FileState::Removed) {
            out.push_back(row);
        }
    }
    auto rank = [](const Dfs::DirRow& row) {
        return row.state == Dfs::FileState::Ready ? 1 : 0;
    };
    std::sort(out.begin(), out.end(), [&](const Dfs::DirRow& a, const Dfs::DirRow& b) {
        if (rank(a) != rank(b))
            return rank(a) > rank(b);
        return a.last_modified > b.last_modified;
    });
    return out;
}

std::optional<Dfs::DirRow> ChatProfile::find_storage_row(const ActorId& chat_main_id) {
    if (chat_main_id.is_zero()) {
        return std::nullopt;
    }
    // Duplicates happen (local ensure + network sync): pick the freshest row,
    // otherwise reads land on a stale/empty profile copy
    auto rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(owner_->node->dfs()->get_db_instance(),
                                                                chat_main_id);
    if (!rows.has_value()) {
        return std::nullopt;
    }
    // Ready rows are readable locally, so they win; among equals — the freshest
    auto rank = [](const Dfs::DirRow& row) {
        return row.state == Dfs::FileState::Ready ? 1 : 0;
    };
    std::optional<Dfs::DirRow> best;
    for (const auto& row : rows.value()) {
        if (row.name != CHAT_PROFILE || row.state == Dfs::FileState::Removed) {
            continue;
        }
        if (!best.has_value() || rank(row) > rank(*best)
            || (rank(row) == rank(*best) && row.last_modified > best->last_modified)) {
            best = row;
        }
    }
    return best;
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
    if (chat_actor_id.is_zero()) {
        return std::unexpected(ChatError::NoChatActor);
    }

    auto previous = read_avatar(chat_actor_id);

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

    // Remove old avatar files after the dictionary points at the new ones.
    if (previous.has_value()) {
        const auto& old = previous.value();
        if (!old.full_id.empty() && old.full_id != avatar.full_id) {
            auto removed = owner_->node->dfs()->remove_stored_file(chat_actor_id, old.full_id);
            if (!removed.has_value()) {
                eWarning("[ChatProfile] Failed to remove old full avatar {}", old.full_id);
            }
        }
        if (!old.mini_id.empty() && old.mini_id != avatar.mini_id) {
            auto removed = owner_->node->dfs()->remove_stored_file(chat_actor_id, old.mini_id);
            if (!removed.has_value()) {
                eWarning("[ChatProfile] Failed to remove old mini avatar {}", old.mini_id);
            }
        }
    }
    return avatar;
}

std::expected<std::string, ChatProfileError> ChatProfile::read_entry(const ActorId&     chat_main_id,
                                                                     const std::string& key) {
    auto rows = profile_storage_rows(owner_->node, chat_main_id);
    if (rows.empty()) {
        return std::unexpected(ChatProfileError::NoProfile);
    }
    for (const auto& row : rows) {
        auto value = owner_->node->dfs()->read_dictionary(chat_main_id, row.file_id, key);
        if (value.has_value()) {
            return value.value();
        }
    }
    return std::unexpected(ChatProfileError::NoEntry);
}

std::expected<std::string, ChatProfileError> ChatProfile::read_name(const ActorId& chat_main_id) {
    return read_entry(chat_main_id, "name");
}

std::expected<std::string, ChatProfileError> ChatProfile::read_bio(const ActorId& chat_main_id) {
    return read_entry(chat_main_id, "bio");
}

std::expected<Chat::ChatProfileAvatar, ChatProfileError> ChatProfile::read_avatar(const ActorId& chat_main_id) {
    auto value = read_entry(chat_main_id, "avatar");
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    auto avatar = Json::deserialize<Chat::ChatProfileAvatar>(value.value());
    if (!avatar.has_value()) {
        return std::unexpected(ChatProfileError::Invalid);
    }
    return std::move(avatar).value();
}
