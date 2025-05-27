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

#include "utils/thread_pool_boost.h"

#include <QTimer>

LoadManager::LoadManager(ExtraChainNode* node, QObject *parent)
    : QObject(parent), node(node) {
    m_timer = new QTimer(this);
    // connect(m_timer, &QTimer::timeout, this, &LoadManager::timer_runner);
    // m_timer->start(2000);
}

void LoadManager::timer_runner()
{
    //Todo: here should be code for timer which should check if we need to send request for file download once more to another identifier
}

void LoadManager::add_to_queue(const ActorId& owner_id, const Dfs::DirRow& dir_row, std::string identifier) {
    auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = dir_row.file_id };

    // Don't add if already in queue or active downloads
    if (m_active_downloads->contains(file_link)) {
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

            if (row->state == Dfs::FileState::Removed) {
                return;
            }

            if (row->type == Dfs::FileType::File && file_path->exists()) {
                auto size = file_path->file_size();
                if (size.has_value() && size == row->size) {
                    return;
                }

                if (row->last_modified > dir_row.last_modified) {
                    return;
                }
            }

            if (row->type != Dfs::FileType::File && file_path->exists()) {
                // return; // TODO: vectorupdate
            }

            if (row->type == Dfs::FileType::Vector && file_path->exists() && row->hash == dir_row.hash) {
                auto res = node->dfs()->make_vector(owner_id,
                                                    dir_row.file_id,
                                                    false,
                                                    node->accountController()->system_actor().id());
                if (res.has_value()) {
                    auto& [dir_row, dfs_vector] = res.value();
                    if (row.has_value()) {
                        auto vector_file_hash = dfs_vector.calculate_template_file_hash();
                        if (vector_file_hash.has_value()) {
                            if (dir_row.hash == vector_file_hash.value().first) {
                                return;
                            }
                        }
                        auto hash_size = dfs_vector.data_hash_size();
                        if (dir_row.hash == hash_size->first) {
                            return;
                        }
                    }
                }
            }
        }
    }

    // check duplicate
    if (node->dfs()->is_file_already_downloaded(owner_id, dir_row.file_id, dir_row.hash)) {
        return;
    }

    eLog("Adding file to download queue: {} / {}", owner_id, dir_row);

    auto load_info = LoadInfo { .dir_row = dir_row, .last_attempt = std::chrono::system_clock::now(), .identifier_list = {identifier} };
    load_info.dir_row.state = Dfs::FileState::Known;

    Responder responder(nullptr);
    responder.add_identifier(identifier);
    this->node->network()->send_message(file_link,
                                        MessageType::DfsFileRequest,
                                        SendMode::Focused,
                                        MessageStatus::NoStatus,
                                        responder);

    m_active_downloads->emplace(file_link, load_info);
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
            if (row.type == Dfs::FileType::File && !need_load) {
                continue;
            }

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

void LoadManager::share_stored_file(const ActorId&     owner_id, const std::string& file_id, const Responder&   responder) {
    auto path = Dfs::Path::file_path(owner_id, file_id);
    if (!path.has_value()) {
        return;
    }

    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }

    auto size = path->file_size();
    if (!size.has_value()) {
       return;
    }
    const uint64_t total_size = size.value();

    if (dir_row->type != Dfs::FileType::File) {
        if (dir_row->type == Dfs::FileType::Collection) {
            node->dfs()->network_request_collection(owner_id, file_id, responder);
        }
        if (dir_row->type == Dfs::FileType::Vector) {
            node->dfs()->network_request_vector(owner_id, file_id, responder);
        }
        return;
    }

    std::string identifier = *responder.identifiers().begin();
    ThreadPoolBoost::instance_dfs()->post([this, identifier, total_size, owner_id, file_id, path = *path, dir_row](){
        uint64_t       offset     = 0;

        while (offset < total_size) {
            auto chunk = Utils::read_file_chunk(path, offset, Dfs::Basic::FRAGMENT_SIZE);
            if (!chunk.has_value()) {
                eCritical("[Dfs] LoadManager::share_stored_file, empy file chunk. owner_id: {}, file_id: {}, offset: {}", owner_id, file_id, offset);
                return;
            }

            Dfs::Packets::FragmentData file_fragment;
            file_fragment.owner_id = owner_id;
            file_fragment.file_id  = file_id;
            file_fragment.full_size  = total_size;
            file_fragment.data     = std::move(*chunk);
            file_fragment.offset   = offset;
            file_fragment.current_size   = chunk->size();
            if (!this->node->network()->isActiveConnectionExists()) {
                eCritical("[Dfs] LoadManager::share_stored_file, no active connections. Cannot share file. owner_id: {}, file_id: {}, offset: {}", owner_id, file_id, offset);
                return;
            }

            Responder responder(nullptr);
            responder.add_identifier(identifier);

            auto message_id = this->node->network()->send_message(file_fragment,
                                                                  MessageType::DfsFileFragment,
                                                                  SendMode::Focused,
                                                                  MessageStatus::NoStatus,
                                                                  responder);
            eLog("[Dfs] LoadManager::share_stored_file, file fragment sended (message_id: {}). owner_id: {}, file_id: {}, offset: {}", message_id, owner_id, file_id, offset);

            offset += Dfs::Basic::FRAGMENT_SIZE;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        emit node->dfs()->uploaded(owner_id, dir_row.value());
    });
    eLog("[Dfs] LoadManager::share_stored_file, file pushed to waiting send queue. owner_id: {}, file_id: {}", owner_id, file_id);
}

void LoadManager::broadcast_file_exist(const ActorId& owner_id, const std::string& file_id)
{
    auto path = Dfs::Path::file_path(owner_id, file_id);
    if (!path.has_value()) {
        return;
    }

    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }

    auto size = path->file_size();
    if (!size.has_value()) {
        return;
    }
    const uint64_t total_size = size.value();

    if (dir_row->type != Dfs::FileType::File) {
        if (dir_row->type == Dfs::FileType::Collection) {
            node->dfs()->network_request_collection(owner_id, file_id, {});
        }
        if (dir_row->type == Dfs::FileType::Vector) {
            node->dfs()->network_request_vector(owner_id, file_id, {});
        }
        return;
    }

    Dfs::Packets::FragmentData file_fragment;
    file_fragment.owner_id = owner_id;
    file_fragment.file_id  = file_id;
    file_fragment.full_size  = total_size;
    if (!this->node->network()->isActiveConnectionExists()) {
        eCritical("[Dfs] LoadManager::brodcast_file_exist, no active connections. Cannot broadcast that file exist. owner_id: {}, file_id: {}", owner_id, file_id);
        return;
    }

    auto message_id = this->node->network()->send_message(file_fragment,
                                                          MessageType::DfsFileExistNotification,
                                                          SendMode::Broadcast,
                                                          MessageStatus::NoStatus);
    eLog("[Dfs] LoadManager::brodcast_file_exist, file fragment sended (message_id: {}). owner_id: {}, file_id: {}", message_id, owner_id, file_id);
}

void LoadManager::file_fragment_achieved(const Dfs::Packets::FragmentData& file_content) {
    ThreadPoolBoost::instance_dfs()->post([this, file_content](){
        const auto file_link = Dfs::FileLink { .owner_id = file_content.owner_id, .file_id = file_content.file_id };
        eLog("[Dfs] LoadManager::file_fragment_achieved, achieved fragment to save. file_link: {}, offset: {}", file_link, file_content.offset);

        {
            auto active_reads_locked = *m_active_reads;
            auto item = active_reads_locked->find(file_link);
            if (item != active_reads_locked->end())
            {
                if (item->second.offsets_read_progress.contains(file_content.offset))
                {
                    eCritical("[Dfs] LoadManager::file_fragment_achieved, offset already exist. file_link: {}, offset: {}", file_link, file_content.offset);
                    return;
                }
                else
                    item->second.offsets_read_progress.emplace(file_content.offset, false);
            }
            else
            {
                ReadStorage read_storage {.current_size = file_content.current_size, .total_size = file_content.full_size};
                read_storage.offsets_read_progress.emplace(file_content.offset, false);

                active_reads_locked->emplace(file_link, read_storage);
            }
        }

        const auto path = Dfs::Path::file_path(file_link.owner_id, file_link.file_id);
        if (!path.has_value()) {
            return;
        }


        {
            std::lock_guard<std::mutex> m_lock(m_write_file_mutex);
            auto result = Utils::write_file_chunk(path.value(), file_content.data, file_content.offset);
            if (!result.has_value()) {
                eCritical("[Dfs] LoadManager::file_content_achieved, save file to disk error. file_link: {}, offset: {}", file_link, file_content.offset);
                return;
            }
        }

        {
            auto active_reads_locked = *m_active_reads;
            auto item = active_reads_locked->find(file_link);
            if (item != active_reads_locked->end())
            {
                auto offset_storage_it = item->second.offsets_read_progress.find(file_content.offset);
                if (offset_storage_it != item->second.offsets_read_progress.end())
                {
                    offset_storage_it->second = true;
                    item->second.current_size += file_content.current_size;

                    if (item->second.current_size >= item->second.total_size)
                    {
                        bool all_offset_finished = true;
                        for (auto offset_it : item->second.offsets_read_progress)
                        {
                            if(!offset_it.second)
                            {
                                all_offset_finished = false;
                                break;
                            }
                        }

                        if (all_offset_finished)
                        {
                            active_reads_locked->erase(item);

                            auto active_downloads = *m_active_downloads;
                            auto res                         = active_downloads->find(file_link);
                            if (res != active_downloads->end())
                            {
                                auto dir_row = res->second.dir_row;
                                bool is_downloaded = node->dfs()->is_file_already_downloaded(file_link.owner_id,
                                                                                             file_link.file_id,
                                                                                             dir_row.hash);
                                if (!is_downloaded) {
                                    eLog("[Fragment] Ooops, something wrong. Need to implement Fragments checks (not downloaded)");
                                    return;
                                }

                                active_downloads->erase(res);
                                eLog("[Dfs] LoadManager::file_content_achieved, file downloaded: {}", file_link);

                                finish_him(file_link.owner_id, dir_row);
                            }
                        }
                    }
                }
            }
        }
    });
}

void LoadManager::finish_him(const ActorId& owner_id, const Dfs::DirRow& dir_row) {
    Dfs::Tables::ActorDirFile::update_file_state(owner_id, dir_row.file_id, Dfs::FileState::Ready);
    emit node->dfs()->added(owner_id, dir_row);
    emit node->dfs()->downloaded(owner_id, dir_row);
}
