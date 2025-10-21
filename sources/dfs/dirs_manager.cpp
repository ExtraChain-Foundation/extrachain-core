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
#include "chain/actor_index.h"
#include "utils/thread_pool_boost.h"

DirsManager::DirsManager(ExtraChainNode* node)
    : QObject(node), node(node) {
    // create dfs folder
    std::filesystem::create_directories(DfsB::DFS_FOLDER);

    // basic creation of dirs file
    auto db_res = Dfs::Tables::DirsFile::DirsSpace::create_file();
    if (!db_res.has_value()) {
        eFatal("[DirsManager] Can't create basic .dirs file");
    }
    db_ = db_res.value();


    //OLD DFS -> NEW DFS converter
    old_dfs_to_new_dfs_converter();
}

DirsManager::~DirsManager()
{
    db_->close();
}

void DirsManager::old_dfs_to_new_dfs_converter()
{
    auto copy_data = [&](const std::string& dir_file, const std::string& owner_id) -> bool {
        std::unique_ptr<DbConnector> db_old = std::make_unique<DbConnector>(dir_file);
        if (!db_old->open()) {
            eCritical("DirsManager::old_dfs_to_new_dfs_converter, Can't open .dir file");
            return false;
        }

        char* errMsg = nullptr;



        static const std::string select_old_query =
            "SELECT file_id, prev_file_id, actor_id, hash, folder, name, size, "
            "created, last_modified, type, encryption, state, sign FROM Files;";

        auto old_db_data = db_old->select(select_old_query);
        db_old->close();

        db_->query("BEGIN TRANSACTION");
        for (auto& db_row : old_db_data)
        {
            db_row.emplace("owner_id", owner_id);
            db_->insert(Dfs::Tables::DirsFile::TableNameActorsFiles, db_row);
        }
        db_->query("COMMIT");
        return true;
    };
    try {
        static std::filesystem::path tempFileIsConverted = Dfs::Basic::DFS_FOLDER + "/.converted";
        if (std::filesystem::exists(tempFileIsConverted)) {
            eInfo("DirsManager::old_dfs_to_new_dfs_converter, .converted file already exists.");
            return;
        }

        if (!std::filesystem::exists(Dfs::Basic::DFS_FOLDER) || !std::filesystem::is_directory(Dfs::Basic::DFS_FOLDER)) {
            eCritical("DirsManager::old_dfs_to_new_dfs_converter, directory {} not exist.", Dfs::Basic::DFS_FOLDER);
            return;
        }

        emit convertion_started();

        int processed_files = 0;
        int deleted_files = 0;
        size_t total = std::distance(std::filesystem::directory_iterator(Dfs::Basic::DFS_FOLDER), std::filesystem::directory_iterator{});
        eLog("Total entries: {}", total);

        for (const auto& entry : std::filesystem::directory_iterator(Dfs::Basic::DFS_FOLDER)) {
            if (entry.is_directory()) {
                std::string sub_dir = entry.path().string();
                std::string sub_dir_name = entry.path().filename().string();
                std::string dir_file = sub_dir + "/.dir";

                if (std::filesystem::exists(dir_file)) {
                    processed_files++;

                    if (copy_data(dir_file, sub_dir_name)) {
                        try {
                            std::filesystem::remove(dir_file);
                            eLog("DirsManager::old_dfs_to_new_dfs_converter, file deleted {}.", dir_file);
                            deleted_files++;
                        } catch (const std::filesystem::filesystem_error& e) {
                            eCritical("DirsManager::old_dfs_to_new_dfs_converter, file deletion '{}' error: {}", dir_file, e.what());
                        }
                    } else {
                        eCritical("DirsManager::old_dfs_to_new_dfs_converter, copy data error. File is not deleted: {}", dir_file);
                    }
                }

                if (std::filesystem::is_empty(sub_dir)) {
                    std::filesystem::remove(sub_dir);
                    eLog("DirsManager::old_dfs_to_new_dfs_converter, empty folder deleted {}.", sub_dir);
                }
            }
        }

        std::ofstream tempFile(tempFileIsConverted);
        if (tempFile) {
            tempFile << "DFS converted\n";
            tempFile.close();
            eLog("DirsManager::old_dfs_to_new_dfs_converter, .converted file created.");
        } else
            eLog("DirsManager::old_dfs_to_new_dfs_converter, .converted file cannot be created.");

        eLog("DirsManager::old_dfs_to_new_dfs_converter, work done. Proccessed files: {}, Deleted files: {}.", processed_files, deleted_files);

        emit convertion_finished();
    } catch (const std::filesystem::filesystem_error& e) {
        eCritical("DirsManager::old_dfs_to_new_dfs_converter, filesystem error: {}", e.what());
    }
}

void DirsManager::update_dirs(const ActorId& actor_id, uint64_t last_modified) {
    auto max_last_modified = Dfs::Tables::DirsFile::DirsSpace::max_last_modified(db_);
    if (!max_last_modified.has_value()) {
        return;
    }
    if (last_modified <= max_last_modified.value()) {
        return;
    }

    Dfs::Tables::DirsFile::DirsSpace::update_row(db_, actor_id, last_modified);
}

void DirsManager::sync(const std::string& identifier) {
    return;
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
    auto max_last_modified = Dfs::Tables::DirsFile::DirsSpace::max_last_modified(db_);
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
    auto allall = Dfs::Tables::DirsFile::DirsSpace::load_from_modified(db_, last_modified);
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

void DirsManager::network_response_from_last_modified(const std::vector<Dfs::Tables::DirsFile::DirsSpace::DirsRow>& dirs_rows,
                                                      const Responder&                           responder) {
    return;

    // eTemp("!_!_!_! {}", dirs_rows);
    std::vector<ActorId> actors;
    actors.reserve(dirs_rows.size());

    for (const auto& dirs_row : dirs_rows) {
        // actors.push_back(dirs_row.actor_id);
        auto last_modified = Dfs::Tables::DirsFile::DirsSpace::last_modified(db_, dirs_row.actor_id);
        if (!last_modified.has_value()) {
            return;
        }
        if (dirs_row.last_modified == last_modified) {
            continue;
        }

        responder.send_response(Dfs::Tables::DirsFile::DirsSpace::DirsRow { .actor_id      = dirs_row.actor_id,
                                                         .last_modified = last_modified.value() },
                                MessageType::DfsSyncDirRows,
                                SendMode::Focused,
                                MessageStatus::Request);
    }
}

void DirsManager::network_request_dir_rows(const Dfs::Tables::DirsFile::DirsSpace::DirsRow& dirs_row, const Responder& responder) {
    return;

    auto dir_rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(db_, dirs_row.actor_id, dirs_row.last_modified);

    if (!dir_rows.has_value()) {
        return;
    }

    responder.send_response(std::make_pair(dirs_row.actor_id, dir_rows.value()),
                            MessageType::DfsSyncDirRows,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void DirsManager::network_response_dir_rows(
    const std::vector<std::pair<ActorId, std::vector<Dfs::DirRow>>> response_data,
    const Responder&                                                responder) {
    ThreadPoolBoost::instance_dfs()->post([this, response_data = std::move(response_data), responder]() {
        for (auto& [owner_id, dir_rows] : response_data) {
            // eTemp("~~~~~~~~~~~~~~~~ {}", dir_rows);
            // TODO: add merge for sync dir file

            Dfs::initialize_actor_folder(owner_id);

            /*
            auto local_dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows_map(owner_id);
            if (local_dir_rows.has_value()) {
                for (const auto& network_row : dir_rows) {
                    auto it = local_dir_rows->find(network_row.file_id);

                     if (it != local_dir_rows->end() && network_row.last_modified != it->second.last_modified) {
                         eLog("Need to update: {} / {}, {}", owner_id, network_row.file_id,
            network_row.last_modified);
                     }
                 }
             }
             */

            // for removed
            for (const auto& row : dir_rows) {
                if (row.type == Dfs::FileType::File && row.state == Dfs::FileState::Removed) {
                    auto file_path = Dfs::Path::file_path(owner_id, row.file_id);
                    if (!file_path.has_value()) {
                        continue;
                    }

                    if (file_path->exists()) {
                        node->dfs()->remove_local_file(owner_id, row.file_id);
                        Dfs::Tables::DirsFile::ActorSpace::update_file_state(db_, owner_id,
                                                                     row.file_id,
                                                                     Dfs::FileState::Removed);
                    }
                }
            }

            // Need to change adding
            auto [res, dir_rows_res] = Dfs::Tables::DirsFile::ActorSpace::add_dir_rows(db_, owner_id, dir_rows);

            // eTemp("~~~~~~~~~~~~~~~~b {}", res);

            if (dir_rows_res.empty()) {
                return;
            }

            auto max_value = std::ranges::max(dir_rows_res, {}, &Dfs::DirRow::last_modified).last_modified;
            this->update_dirs(owner_id, max_value);

            if (!node_enabled.load()) {
                return;
            }

            if (owner_id == node->network_id()) {
                auto rows = dir_rows;
                for (auto it = rows.begin(); it != rows.end();) {
                    if (it->name == "Usernames") {
                        node->dfs()->request_file(owner_id, it->file_id);
                        it = rows.erase(it);
                    } else {
                        ++it;
                    }
                }

                node->dfs()->download_manager().add_to_queue(owner_id, rows, *responder.identifiers().begin());
                continue;
            }

            node->dfs()->download_manager().add_to_queue(owner_id, dir_rows_res, *responder.identifiers().begin());
        }
    });

    if (!node->dfs()->is_dirs_loaded_) {
        node->dfs()->is_dirs_loaded_ = true;
        emit node->dfs()->dirsLoaded();
    }
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
    ThreadPoolBoost::instance_dfs()->post([this, responder] {
        auto actors = node->actor_index()->read_all_actors_ids();

        auto network_id = node->actor_index()->network_id();
        auto raccoon_id = ActorId("46710a2d823c23db9fc2ac01e0f84212a8128373");
        std::erase_if(actors, [&network_id, &raccoon_id](const ActorId& actor) {
            return actor == network_id || actor == raccoon_id;
        });

        actors.insert(actors.begin(), network_id);
        actors.insert(actors.begin(), raccoon_id);

        std::vector<std::pair<ActorId, std::vector<Dfs::DirRow>>> response_data;
        response_data.reserve(actors.size());

        for (const auto& actor : actors) {
            auto dir_rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(db_, actor, 0);

            if (!dir_rows.has_value() || dir_rows->empty())
                continue;

            response_data.emplace_back(actor, dir_rows.value());

            // QThread::msleep(3);

            if (!node_enabled.load()) {
                return;
            }
        }

        responder.send_response(response_data,
                                MessageType::DfsSyncDirRows,
                                SendMode::Focused,
                                MessageStatus::Response);
    });
}

std::shared_ptr<DbConnector> DirsManager::get_db_instance()
{
    return db_;
}
