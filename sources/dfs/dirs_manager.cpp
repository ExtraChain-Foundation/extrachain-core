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
#include "dfs/dfs_controller.h"
#include "dfs/load_manager.h"
#include "utils/exc_logs.h"

#include "blockchain/actor_index.h"

DirsManager::DirsManager(ExtraChainNode* node)
    : node(node) {
    // create dfs folder
    std::filesystem::create_directories(DfsB::DFS_FOLDER);

    // basic creation of dirs file
    bool dirs_result = Dfs::DirsFile::create_file();
    if (!dirs_result) {
        eFatal("[DirsManager] Can't create basic .dirs file");
    }
}

void DirsManager::update_dirs(const ActorId& actor_id, uint64_t last_modified) {
    auto max_last_modified = Dfs::DirsFile::max_last_modified();
    if (!max_last_modified.has_value()) {
        return;
    }
    if (last_modified <= max_last_modified.value()) {
        return;
    }

    Dfs::DirsFile::update_row(actor_id, last_modified);
}

void DirsManager::sync(const std::string& identifier) {
    if (identifier.empty()) {
        return;
    }

    Responder responder(nullptr);
    responder.add_identifier(identifier);
    node->network()->send_message(0,
                                  MessageType::DfsSyncDirs,
                                  SendMode::Focused,
                                  MessageStatus::Request,
                                  responder);
}

void DirsManager::network_request_sync(const Responder& responder) {
    auto max_last_modified = Dfs::DirsFile::max_last_modified();
    if (!max_last_modified.has_value()) {
        eFatal("[Dfs] Sync error");
    }

    responder.send_response(max_last_modified.value(),
                            MessageType::DfsSyncDirs,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void DirsManager::network_response_sync(uint64_t max_last_modified, const Responder& responder) {
    // eTemp("--------------- {} ", max_last_modified);
    send_from_last_modified(max_last_modified, responder);
}

void DirsManager::send_from_last_modified(uint64_t last_modified, const Responder& responder) {
    if (last_modified > 300'000)
        last_modified -= 300'000;
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
    responder.send_response(allall.value(),
                            MessageType::DfsSyncDirsRows,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void DirsManager::network_response_from_last_modified(const std::vector<Dfs::DirsFile::DirsRow>& dirs_rows,
                                                      const Responder&                           responder) {
    // eTemp("!_!_!_! {}", dirs_rows);
    std::vector<ActorId> actors;
    actors.reserve(dirs_rows.size());

    for (const auto& dirs_row : dirs_rows) {
        // actors.push_back(dirs_row.actor_id);
        auto last_modified = Dfs::DirsFile::last_modified(dirs_row.actor_id);
        if (!last_modified.has_value()) {
            return;
        }
        if (dirs_row.last_modified == last_modified) {
            continue;
        }

        responder.send_response(Dfs::DirsFile::DirsRow { .actor_id      = dirs_row.actor_id,
                                                         .last_modified = last_modified.value() },
                                MessageType::DfsSyncDirRows,
                                SendMode::Focused,
                                MessageStatus::Request);
    }
}

void DirsManager::network_request_dir_rows(const Dfs::DirsFile::DirsRow& dirs_row, const Responder& responder) {
    auto dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows(dirs_row.actor_id, dirs_row.last_modified);

    if (!dir_rows.has_value()) {
        return;
    }

    responder.send_response(std::make_pair(dirs_row.actor_id, dir_rows.value()),
                            MessageType::DfsSyncDirRows,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void DirsManager::network_response_dir_rows(const ActorId&                  owner_id,
                                            const std::vector<Dfs::DirRow>& dir_rows,
                                            const Responder&                responder) {
    // eTemp("~~~~~~~~~~~~~~~~ {}", dir_rows);
    // TODO: add merge for sync dir file

    Dfs::initialize_actor_folder(owner_id);

    /*
    auto local_dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows_map(owner_id);
    if (local_dir_rows.has_value()) {
        for (const auto& network_row : dir_rows) {
            auto it = local_dir_rows->find(network_row.file_id);

            if (it != local_dir_rows->end() && network_row.last_modified != it->second.last_modified) {
                eLog("Need to update: {} / {}, {}", owner_id, network_row.file_id, network_row.last_modified);
            }
        }
    }
    */

    // Need to change adding
    auto res = Dfs::Tables::ActorDirFile::add_dir_rows(owner_id, dir_rows);

    // eTemp("~~~~~~~~~~~~~~~~b {}", res);

    if (dir_rows.empty()) {
        return;
    }
    auto max_value = std::ranges::max(dir_rows, {}, &Dfs::DirRow::last_modified).last_modified;
    this->update_dirs(owner_id, max_value);

    node->dfs()->download_manager().add_to_queue(owner_id, dir_rows, *responder.identifiers().begin());
}

void DirsManager::temp_sync_all(const std::string& identifier) {
    Responder responder(nullptr);
    responder.add_identifier(identifier);
    node->network()->send_message(true,
                                  MessageType::DfsTempSyncAll,
                                  SendMode::Focused,
                                  MessageStatus::Response,
                                  responder);
}

void DirsManager::network_request_all(const Responder& responder) {
    auto actors = node->actorIndex()->allActors();

    auto network_id = node->actorIndex()->network_id();
    auto raccoon_id = ActorId("46710a2d823c23db9fc2ac01e0f84212a8128373");
    std::erase_if(actors, [&network_id, &raccoon_id](const ActorId& actor) {
        return actor == network_id || actor == raccoon_id;
    });

    actors.insert(actors.begin(), network_id);
    actors.insert(actors.begin(), raccoon_id);

    for (const auto& actor : actors) {
        auto dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows(actor, 0);

        if (!dir_rows.has_value()) {
            return;
        }

        responder.send_response(std::make_pair(actor, dir_rows.value()),
                                MessageType::DfsSyncDirRows,
                                SendMode::Focused,
                                MessageStatus::Response);
    }
}
