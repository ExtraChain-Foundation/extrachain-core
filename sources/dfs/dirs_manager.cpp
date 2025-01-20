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

#include "dfs/dirs_manager.h"

#include "managers/extrachain_node.h"
#include "network/network_manager.h"
#include "dfs/download_manager.h"
#include "utils/exc_logs.h"

// тебе нужно будет добавить:
// #include "blockchain/dirs.h" - для работы с .dirs файлом
// #include "network/dfs_sync_messages.h"

DirsManager::DirsManager(ExtraChainNode* node)
    : node(node) {
    // create dfs folder
    std::filesystem::create_directories(DfsB::fsActrRoot);

    // basic creation of dirs file
    bool dirs_result = Dfs::DirsFile::create_file();
    if (!dirs_result) {
        eFatal("[DirsManager] Can't create basic .dirs file");
    }
}

void DirsManager::initialize_actor_folder(const ActorId& actorId) {
    std::string path_delim = Utils::platformDelimeter();
    std::filesystem::create_directories(DfsB::fsActrRoot + path_delim + actorId.to_string());
    DbConnector dir_file = DfsT::ActorDirFile::get_actor_dir_file(actorId);
    dir_file.query(DfsT::ActorDirFile::CreateTableQuery);
    // requestDirData(actorId);
}

void DirsManager::update_dirs(const ActorId& actor_id, uint64_t last_modified) {
    auto max_last_modified = Dfs::DirsFile::max_last_modified();
    if (!max_last_modified.has_value()) {
        return;
    }
    if (last_modified <= max_last_modified.value()) {
        return;
    }

    auto dirs_row = Dfs::DirsFile::DirsRow { .actor_id = actor_id, .last_modified = last_modified };
    Dfs::DirsFile::insert(dirs_row);
}

void DirsManager::sync(const std::string& identifier) const {
    if (identifier.empty()) {
        return;
    }

    node->network()->send_message(0,
                                  MessageType::DfsSyncDirs,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Request,
                                  "",
                                  identifier);
}

void DirsManager::network_request_sync(const std::string& message_id) const {
    auto max_last_modified = Dfs::DirsFile::max_last_modified();
    if (!max_last_modified.has_value()) {
        eFatal("[Dfs] Sync error");
    }

    node->network()->send_message(max_last_modified.value(),
                                  MessageType::DfsSyncDirs,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  message_id);
}

void DirsManager::network_response_sync(uint64_t max_last_modified, const std::string& message_id) const {
    eLog("--------------- {} ", max_last_modified);
    send_from_last_modified(max_last_modified, message_id);
}

void DirsManager::send_from_last_modified(uint64_t last_modified, const std::string& message_id) const {
    auto allall = Dfs::DirsFile::load_from_modified(last_modified);
    if (!allall.has_value()) {
        return;
    }

    if (allall.value().empty()) {
        return;
    }

    // std::vector<ActorId> actors;
    // for (const auto& dirs_row : allall.value()) {
    //     actors.push_back(dirs_row.actor_id);
    // }
    node->network()->send_message(allall.value(),
                                  MessageType::DfsSyncDirsRows,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  "",
                                  message_id);
}

void DirsManager::network_response_from_last_modified(const std::vector<Dfs::DirsFile::DirsRow>& dirs_rows,
                                                      const std::string& identifier) const {
    eTemp("!_!_!_! {}", dirs_rows);
    std::vector<ActorId> actors;
    actors.reserve(dirs_rows.size());

    for (const auto& dirs_row : dirs_rows) {
        // actors.push_back(dirs_row.actor_id);
        auto last_modified = Dfs::DirsFile::last_modified(dirs_row.actor_id);
        if (!last_modified.has_value()) {
            return;
        }

        node->network()->send_message(Dfs::DirsFile::DirsRow { .actor_id      = dirs_row.actor_id,
                                                               .last_modified = last_modified.value() },
                                      MessageType::DfsSyncDirRows,
                                      Config::Net::TypeSend::Focused,
                                      MessageStatus::Request,
                                      "",
                                      identifier);
    }
}

void DirsManager::network_request_dir_rows(const Dfs::DirsFile::DirsRow& dirs_row,
                                           const std::string&            message_id) const {
    auto dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows(dirs_row.actor_id, dirs_row.last_modified);

    if (!dir_rows.has_value()) {
        return;
    }
    node->network()->send_message(std::make_pair(dirs_row.actor_id, dir_rows.value()),
                                  MessageType::DfsSyncDirRows,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  message_id);
}

void DirsManager::network_response_dir_rows(const ActorId&                  owner_id,
                                            const std::vector<Dfs::DirRow>& dir_rows,
                                            const std::string&              message_id) const {
    eTemp("~~~~~~~~~~~~~~~~ {}", dir_rows);
    // TODO: add merge for sync dir file

    auto res = Dfs::Tables::ActorDirFile::add_dir_rows(owner_id, dir_rows);
    eTemp("~~~~~~~~~~~~~~~~b {}", res);
}
