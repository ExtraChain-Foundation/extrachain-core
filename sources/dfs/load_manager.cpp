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

#include "dfs/load_manager.h"

#include "managers/extrachain_node.h"
#include "network/network_manager.h"
#include "dfs/dfs_controller.h"
#include "utils/exc_logs.h"
#include "dfs/dfs_utils.h"

// Implementation file will contain network-related includes that you'll add later
// #include "network/dfs_sync_messages.h"

LoadManager::LoadManager(ExtraChainNode* node)
    : node(node) {
    // load known from all dir
}

void LoadManager::add_to_queue(const ActorId& owner_id, const Dfs::DirRow& dir_row, std::string identifier) {
    auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = dir_row.file_id };

    // Don't add if already in queue or active downloads
    if (active_downloads.contains(file_link)) {
        return;
    }

    if (dir_row.type == Dfs::FileType::File && dir_row.state != Dfs::FileState::Ready) {
        return;
    }

    auto row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, dir_row.file_id);
    if (row.has_value()) {
        if (row->state == Dfs::FileState::Ready || row->state == Dfs::FileState::Partial) {
            auto file_path = Dfs::Path::file_path(owner_id, dir_row.file_id);
            if (!file_path.has_value()) {
                return;
            }

            if (row->type == Dfs::FileType::File && file_path->exists()) {
                auto size = file_path->file_size();
                if (size.has_value() && size == row->size) {
                    return;
                }
            }

            if (row->type != Dfs::FileType::File && file_path->exists()) {
                // return; // TODO: vectorupdate
            }

            if (row->type != Dfs::FileType::Vector && file_path->exists()) {
            }
        }
    }

    // check duplicate
    if (node->dfs()->is_file_already_downloaded(owner_id, dir_row.file_id, dir_row.hash)) {
        return;
    }

    eLog("Adding file to download queue: {} / {}", owner_id, dir_row);

    // // Check in queue
    // bool file_in_queue =
    //     std::any_of(download_queue.cbegin(), download_queue.cend(), [&file_link](const auto& info) {
    //         return false; // info.dir_row == file_link;
    //     });

    // if (file_in_queue) {
    //     return;
    // }

    auto load_info = LoadInfo { .dir_row = dir_row, .last_attempt = std::chrono::system_clock::now() };
    // check real status
    load_info.dir_row.state = Dfs::FileState::Known;
    active_downloads.insert({ file_link, load_info });
    // download_queue.push(load_info);

    // temp: request file
    Responder responder(nullptr);
    responder.add_identifier(identifier);
    this->node->network()->send_message(file_link,
                                        MessageType::DfsFileRequest,
                                        SendMode::Focused,
                                        MessageStatus::NoStatus,
                                        responder);

    // Try to process queue if we have space for new downloads
    if (active_downloads.size() < MAX_CONCURRENT_DOWNLOADS) {
        // process_next();
    }
}

void LoadManager::add_to_queue(const ActorId&                  owner_id,
                               const std::vector<Dfs::DirRow>& dir_rows,
                               std::string                     identifier) {
    bool is_full   = node->dfs()->mode() == DfsMode::Full;
    bool need_load = is_full || node->dfs()->is_priority(owner_id);

    for (const auto& dir_row : dir_rows) {
        if (dir_row.state == Dfs::FileState::Removed) {
            continue;
        }

        if (dir_row.type == Dfs::FileType::File && !need_load) {
            continue;
        }

        add_to_queue(owner_id, dir_row, identifier);
    }
}

// TODO: move to dirs manager
void LoadManager::check_all_files(std::string identifier) {
    auto dirs = Dfs::DirsFile::load_all();
    if (!dirs.has_value()) {
        return;
    }

    for (const auto& dir : dirs.value()) {
        bool is_full   = node->dfs()->mode() == DfsMode::Full;
        bool need_load = is_full || node->dfs()->is_priority(dir.actor_id);

        const auto dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows(dir.actor_id);
        if (!dir_rows.has_value()) {
            //
            continue;
        }

        for (const auto& row : dir_rows.value()) {
            if (row.state == Dfs::FileState::Ready) {
                auto file_path = Dfs::Path::file_path(dir.actor_id, row.file_id);
                if (!file_path.has_value()) {
                    continue;
                }

                if (row.type == Dfs::FileType::File && file_path->exists()) {
                    auto size = file_path->file_size();
                    if (size.has_value() && size == row.size) {
                        continue;
                    }

                    if (!need_load) {
                        continue;
                    }
                }

                if (row.type != Dfs::FileType::File && file_path->exists()) {
                    // TODO: vectorupdate
                    // continue;
                }
                // TODO: add checks for vector and collection
            }

            if (row.state == Dfs::FileState::Removed) {
                continue;
            }

            auto file_link = Dfs::FileLink { .owner_id = dir.actor_id, .file_id = row.file_id };

            // TODO: insert to queue

            // TODO: process from queue
            // search file
            if (identifier.empty()) {
                this->node->network()->send_message(file_link,
                                                    MessageType::DfsFileState,
                                                    SendMode::Neighbours,
                                                    MessageStatus::Request);
            } else {
                Responder responder(nullptr);
                responder.add_identifier(identifier);
                this->node->network()->send_message(file_link,
                                                    MessageType::DfsFileState,
                                                    SendMode::Focused,
                                                    MessageStatus::Request,
                                                    responder);
            }
        }
    }

    // bool is_downloaded = node->dfs()->is_file_already_downloaded(file_link.owner_id,
    //                                                              file_link.file_id,
    //                                                              active_download.dir_row.hash);
    // if (!is_downloaded) {5
    // }
}

// void LoadManager::process_next() {
//     while (active_downloads.size() < MAX_CONCURRENT_DOWNLOADS) {
//         auto next = get_next_download();
//         if (!next) {
//             break; // No more items to process
//         }

//         // send_search_request(*next);

//         next->last_attempt          = std::chrono::system_clock::now();
//         next->last_segment_time     = std::chrono::system_clock::now();
//         active_downloads[file_link] = *next;
//     }
// }

// std::optional<LoadInfo> LoadManager::get_next_download() {
//     while (!download_queue.empty()) {
//         auto info = download_queue.front();
//         download_queue.pop();

//         if (info.attempt_count >= MAX_ATTEMPTS) {
//             eWarning("Max attempts reached for file: {}", info.???);
//             continue;
//         }

//         if (!info.can_retry()) {
//             // Put back in queue if it's too early to retry
//             download_queue.push(info);
//             return std::nullopt;
//         }

//         info.attempt_count++;
//         return info;
//     }
//     return std::nullopt;
// }

// void LoadManager::check_stalled_downloads() {
//     std::vector<Dfs::FileLink> stalled_files;

//     // Find stalled downloads
//     for (const auto& [file_link, info] : active_downloads) {
//         if (info.is_stalled()) {
//             stalled_files.push_back(file_link);
//         }
//     }

//     // Move stalled downloads to queue end
//     for (const auto& file_link : stalled_files) {
//         move_to_queue_end(file_link);
//     }
// }

void LoadManager::move_to_queue_end(const Dfs::FileLink& file_link) {
    auto it = active_downloads.find(file_link);
    if (it == active_downloads.end()) {
        return;
    }

    eWarning("Moving stalled download to queue end: {}", file_link);

    auto info = it->second;
    active_downloads.erase(it);
    download_queue.push(info);

    // Try to start next download
    // process_next();
}

void LoadManager::broadcast_stored_file(const ActorId&     owner_id,
                                        const std::string& file_id,
                                        const Responder&   responder) {
    auto path = Dfs::Path::file_path(owner_id, file_id);
    if (!path.has_value()) {
        return;
    }
    auto size = path->file_size();
    if (!size.has_value()) {
        return;
    }

    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }

    if (dir_row->type != Dfs::FileType::File) {
        if (dir_row->type == Dfs::FileType::Collection) {
            node->dfs()->network_request_collection(owner_id, file_id, responder);
        }
        if (dir_row->type == Dfs::FileType::Vector) {
            node->dfs()->network_request_vector(owner_id, file_id, responder);
        }
        return;
    }

    const uint64_t total_size = size.value();
    uint64_t       offset     = 0;

    std::string identifier = responder.identifiers().empty() ? "" : *responder.identifiers().begin();
    std::thread sender([this, owner_id, file_id, dir_row, path = *path, total_size = *size, identifier]() {
        uint64_t offset = 0;

        while (offset < total_size) {
            auto chunk = Utils::read_file_chunk(path, offset, Dfs::Basic::FRAGMENT_SIZE);
            if (!chunk.has_value()) {
                return;
            }

            Dfs::Packets::FragmentData file_fragment;
            file_fragment.owner_id = owner_id;
            file_fragment.file_id  = file_id;
            file_fragment.data     = std::move(*chunk);
            file_fragment.offset   = offset;

            if (!this->node->network()->isActiveConnectionExists()) {
                return;
            }

            auto message_type = identifier.empty() ? MessageType::DfsStoreFragment : MessageType::DfsFileFragment;
            auto type_send    = identifier.empty() ? SendMode::Broadcast : SendMode::Focused;

            Responder responder(nullptr);
            responder.add_identifier(identifier);

            this->node->network()->send_message(file_fragment,
                                                message_type,
                                                type_send,
                                                MessageStatus::NoStatus,
                                                responder);

            offset += Dfs::Basic::FRAGMENT_SIZE;
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }

        // auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
        // if (!dir_row.has_value()) {
        //     return;
        // }
        // use list for first uploaded
        node->dfs()->uploaded(owner_id, dir_row.value());
    });

    sender.detach();
}

void LoadManager::network_fragment(const Dfs::Packets::FragmentData& fragment_data) {
    auto file_link = Dfs::FileLink { .owner_id = fragment_data.owner_id, .file_id = fragment_data.file_id };

    // TODO: Fragments: verify fragment, use Dir Row and fragment list
    if (fragment_data.data.size() > Dfs::Basic::FRAGMENT_SIZE) {
        eCritical("[Dfs] Incorrect fragment size: {}", fragment_data.data.size());
        return;
    }

    auto path = Dfs::Path::file_path(fragment_data.owner_id, fragment_data.file_id);
    if (!path.has_value()) {
        return;
    }

    auto result = Utils::write_file_chunk(path.value(), fragment_data.data, fragment_data.offset);
    if (!result.has_value()) {
        return;
    }

    if (fragment_data.offset == 0) {
        Dfs::Tables::ActorDirFile::update_file_state(fragment_data.owner_id,
                                                     fragment_data.file_id,
                                                     Dfs::FileState::Partial);
    }

    if (active_downloads.find(file_link) == active_downloads.end()) {
        eWarning("Unknown fragment");
        return;
    }

    auto active_download  = active_downloads.at(file_link);
    bool is_last_fragment = (fragment_data.offset + Dfs::Basic::FRAGMENT_SIZE >= active_download.dir_row.size);
    if (is_last_fragment) {
        bool is_downloaded = node->dfs()->is_file_already_downloaded(file_link.owner_id,
                                                                     file_link.file_id,
                                                                     active_download.dir_row.hash);
        if (!is_downloaded) {
            eLog("[Fragment] Ooops, something wrong. Need to implement Fragments checks (not downloaded)");
            return;
        }
        active_downloads.erase(file_link);
        eLog("[Fragment] Last fragment (downloaded) for {}", file_link);

        finish_him(file_link.owner_id, active_download.dir_row);
    }

    // eTemp("[Fragment] {}: size: {}, offset: {}, {}",
    //       file_link,
    //       active_download.dir_row.size,
    //       fragment_data.offset,
    //       is_last_fragment);
}

void LoadManager::finish_him(const ActorId& owner_id, const Dfs::DirRow& dir_row) {
    Dfs::Tables::ActorDirFile::update_file_state(owner_id, dir_row.file_id, Dfs::FileState::Ready);
    emit node->dfs()->added(owner_id, dir_row);
    emit node->dfs()->downloaded(owner_id, dir_row);
}
