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

LoadManager::LoadManager(ExtraChainNode* node, QObject* parent)
    : QObject(parent)
    , node(node) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        timer_runner();
    });
    m_timer->start(5000);
}

void LoadManager::timer_runner(const Dfs::FileLink file_link_to_proceed) {
    {
        auto amount_file_fragments_requests_locked = *m_amount_file_fragments_requests;
        auto now                                   = std::chrono::system_clock::now();
        for (auto it = amount_file_fragments_requests_locked->begin();
             it != amount_file_fragments_requests_locked->end();) {
            auto duration = now - it->second;
            if (duration > std::chrono::seconds(10))
                it = amount_file_fragments_requests_locked->erase(it);
            else
                ++it;
        }
    }

    auto process_func = [&](SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& active_downloads) -> bool {
        if (!active_downloads->empty() && m_amount_file_fragments_requests->size() <= MAX_CONCURRENT_DOWNLOADS) {
            auto active_downloads_locked = *active_downloads;
            for (auto& it : *active_downloads_locked) {
                if (m_amount_file_fragments_requests->size() >= MAX_CONCURRENT_DOWNLOADS)
                    return false;

                auto& load_info      = it.second;
                bool  ignore_timeout = file_link_to_proceed == it.first;
                bool  is_requested   = false;
                for (auto& identifier : load_info.identifier_list) {
                    if (m_amount_file_fragments_requests->size() >= MAX_CONCURRENT_DOWNLOADS)
                        return false;

                    auto now      = std::chrono::system_clock::now();
                    auto duration = now - identifier.second.last_attempt;
                    if (!node->network()->is_connection_exists(identifier.first)) {
                        // eCritical(
                        //     "LoadManager::timer_runner, connection with identifier ({}) not exist for file_link:
                        //     {}.", identifier.first, it.first);
                        continue;
                    }

                    if (identifier.second.counter >= 3)
                        continue;
                    else if (identifier.second.counter == 0
                             || (duration > std::chrono::seconds(10) || ignore_timeout)) {
                        if (identifier.second.counter == 1 && load_info.identifier_list.size() == 1) {
                            this->node->network()->send_message(it.first,
                                                                MessageType::DfsFileRequestContinueUpload,
                                                                SendMode::Neighbours,
                                                                MessageStatus::Request);
                        }

                        identifier.second.counter++;
                        identifier.second.last_attempt = std::chrono::system_clock::now();

                        Responder responder(nullptr);
                        responder.add_identifier(identifier.first);

                        Dfs::FileLinkFragment output;
                        output.file_link = it.first;

                        if (it.second.amount_fragments > 0) {
                            for (auto number : it.second.fragments_left) {
                                if (m_amount_file_fragments_requests->size() >= MAX_CONCURRENT_DOWNLOADS)
                                    return false;
                                output.fragment_numbers.emplace(number);
                                m_amount_file_fragments_requests->emplace(output, std::chrono::system_clock::now());
                            }
                        } else {
                            output.fragment_numbers.emplace(1);
                            m_amount_file_fragments_requests->emplace(output, std::chrono::system_clock::now());
                        }

                        this->node->network()->send_message(output,
                                                            MessageType::DfsFileRequest,
                                                            SendMode::Focused,
                                                            MessageStatus::NoStatus,
                                                            responder);

                               // eLog("LoadManager::timer_runner, try to send request once more with identifier ({}),
                               // attempt: {} for file_link: {} and fragments: {}.", identifier.first,
                               // identifier.second.counter, it.first, output.fragment_numbers);
                        is_requested = true;
                        break;
                    } else {
                        is_requested = true;
                        break;
                    }
                }
                auto identifier_list_size = load_info.identifier_list.size();
                if (!is_requested && identifier_list_size > 0) {
                    // eCritical("LoadManager::timer_runner, cannot download file. No response from identifiers.
                    // Identifiers list size: {}", identifier_list_size);
                }
            }
            return true;
        }
        return true;
    };

    if (process_func(m_active_downloads_priority))
        process_func(m_active_downloads);
}

void LoadManager::remove_active_download(const Dfs::FileLinkFragment& file_link_fragment) {
    bool is_priority = node->dfs()->is_priority(file_link_fragment.file_link.owner_id);
    if (is_priority)
        m_active_downloads_priority->erase(file_link_fragment.file_link);
    else
        m_active_downloads->erase(file_link_fragment.file_link);
    m_amount_file_fragments_requests->erase(file_link_fragment);
    eLog("m_active_downloads{}->erase: {}", is_priority ? "_priority" : "", file_link_fragment.file_link.hash());
}

bool LoadManager::add_network_identifier(const Dfs::FileLink& file_link, std::string identifier) {
    bool is_priority = node->dfs()->is_priority(file_link.owner_id);

    auto process_func = [&file_link, &identifier, &is_priority](SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& active_downloads)
    {
        auto active_downloads_locked = *active_downloads;
        auto it                      = active_downloads_locked->find(file_link);
        if (it != active_downloads_locked->end()) {
            auto identifier_storage_checker_it = it->second.identifier_storage_checker.emplace(identifier);
            if (identifier_storage_checker_it.second) {
                it->second.identifier_list.emplace_back(identifier, LoadInfo::Attempts { .counter = 0 });
                eLog("m_active_downloads{} update list: {} || {}", is_priority ? "_priority" : "" , file_link.hash(), identifier );
                return true;
            }
        }
        return false;
    };

    return process_func(node->dfs()->is_priority(file_link.owner_id) ? m_active_downloads_priority : m_active_downloads);
}

void LoadManager::add_to_queue(const ActorId&     owner_id,
                               const Dfs::DirRow& dir_row,
                               const std::string& identifier,
                               const bool         notify_neighbours) {
    auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = dir_row.file_id };

    bool is_priority = node->dfs()->is_priority(owner_id);

    if (!node_enabled.load()) {
        return;
    }

    // Don't add if already in queue or active downloads
    {
        auto active_downloads_locked = *m_active_downloads;

        if (active_downloads_locked->contains(file_link)) {
            return;
        }
    }

    bool is_full   = node->dfs()->mode() == DfsMode::Full;
    bool need_load = is_full || is_priority;

    if (/*dir_row.type == Dfs::FileType::File && (dir_row.state != Dfs::FileState::Ready ||*/ !need_load /*)*/) {
        return;
    }

    auto row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(), owner_id, dir_row.file_id);
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
                                                    node->account_controller()->system_actor().id());
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

    // eLog("Adding file to download queue: {} / {}", owner_id, dir_row);

    auto load_info = LoadInfo { .dir_row = dir_row };
    // LoadInfo::Attempts attempts { .counter = 1, .last_attempt = std::chrono::system_clock::now()};
    LoadInfo::Attempts attempts { .counter = 0 };
    load_info.identifier_storage_checker.emplace(identifier);
    load_info.identifier_list.emplace_back(identifier, attempts);

    load_info.dir_row.state = Dfs::FileState::Known;

    load_info.notify_neighbours = notify_neighbours;

    std::pair<std::unordered_map<Dfs::FileLink, LoadInfo>::iterator, bool> res;
    if (is_priority)
        res = m_active_downloads_priority->emplace(file_link, load_info);
    else
        res = m_active_downloads->emplace(file_link, load_info);
    if (res.second) {
        // Responder responder(nullptr);
        // responder.add_identifier(identifier);
        // this->node->network()->send_message(file_link,
        //         MessageType::DfsFileRequest,
        //         SendMode::Focused,
        //         MessageStatus::NoStatus,
        //         responder);
        eLog("m_active_downloads{}->emplace: {}", is_priority ? "_priority" : "", file_link.hash());
    } else {
        // eWarning("LoadManager::add_to_queue, file_link exist: {}. Adding identifier to the list...", file_link);
        add_network_identifier(file_link, identifier);
    }
}

void LoadManager::add_to_queue(const ActorId&                  owner_id,
                               const std::vector<Dfs::DirRow>& dir_rows,
                               const std::string&              identifier) {
    bool is_full   = node->dfs()->mode() == DfsMode::Full;
    bool need_load = is_full || node->dfs()->is_priority(owner_id);

    for (const auto& dir_row : dir_rows) {
        if (dir_row.state == Dfs::FileState::Removed) {
            continue;
        }

        if (/* dir_row.type == Dfs::FileType::File && */ !need_load) {
            continue;
        }

        add_to_queue(owner_id, dir_row, identifier);
    }
}

void LoadManager::share_stored_file(const Dfs::FileLinkFragment& file_link_fragment, const Responder& responder) {
    // eLog("LoadManager::share_stored_file, file_id: {}", file_link_fragment.file_link.file_id);
    auto path = Dfs::Path::file_path(file_link_fragment.file_link.owner_id, file_link_fragment.file_link.file_id);
    if (!path.has_value()) {
        // eCritical("LoadManager::share_stored_file, no path. file_id: {}", file_link_fragment.file_link.file_id);
        return;
    }

    auto dir_row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(), file_link_fragment.file_link.owner_id,
                                                          file_link_fragment.file_link.file_id);
    if (!dir_row.has_value()) {
        // eCritical("LoadManager::share_stored_file, no dir_row. file_id: {}",
        // file_link_fragment.file_link.file_id);
        return;
    }

    auto size = path->file_size();
    if (!size.has_value()) {
        // eCritical("LoadManager::share_stored_file, no size. file_id: {}", file_link_fragment.file_link.file_id);
        return;
    }
    const uint64_t total_size = size.value();

    if (dir_row->type != Dfs::FileType::File) {
        if (dir_row->type == Dfs::FileType::Collection) {
            node->dfs()->network_request_collection(file_link_fragment.file_link.owner_id,
                                                    file_link_fragment.file_link.file_id,
                                                    responder);
            // eCritical("LoadManager::share_stored_file, its a collection. Another thlow. file_id: {}",
            // file_link_fragment.file_link.file_id);
        }
        if (dir_row->type == Dfs::FileType::Vector) {
            node->dfs()->network_request_vector(file_link_fragment.file_link.owner_id,
                                                file_link_fragment.file_link.file_id,
                                                responder);
            // eCritical("LoadManager::share_stored_file, its a vector. Another thlow. file_id: {}",
            // file_link_fragment.file_link.file_id);
        }
        return;
    }

    static auto calculate_max_offsets = [](size_t total_size, size_t buffer_size) -> size_t {
        return (total_size + buffer_size - 1) / buffer_size;
    };

    auto max_offsets = calculate_max_offsets(total_size, Dfs::Basic::FRAGMENT_SIZE);

    std::string identifier = *responder.identifiers().begin();
    ThreadPoolBoost::instance_dfs()->post(
        [this, identifier, max_offsets, total_size, file_link_fragment, path = *path, dir_row]() {
            eLog("instance_dfs share_stored_file in");
            uint64_t offset = 0;

            for (const auto& fragment_number : file_link_fragment.fragment_numbers) {
                offset     = Dfs::Basic::FRAGMENT_SIZE * (fragment_number - 1);
                auto chunk = Utils::read_file_chunk(path, offset, Dfs::Basic::FRAGMENT_SIZE);
                if (!chunk.has_value()) {
                    // eCritical("[Dfs] LoadManager::share_stored_file, empy file chunk. owner_id: {}, file_id: {},
                    // fragment number: {}", file_link_fragment.file_link.owner_id,
                    // file_link_fragment.file_link.file_id, fragment_number);
                    return;
                }
                auto chunk_size = chunk->size();

                Dfs::Packets::FragmentData file_fragment;
                file_fragment.owner_id              = file_link_fragment.file_link.owner_id;
                file_fragment.file_id               = file_link_fragment.file_link.file_id;
                file_fragment.data                  = std::move(*chunk);
                file_fragment.offset                = offset;
                file_fragment.current_size          = chunk_size;
                file_fragment.fragment_number       = fragment_number;
                file_fragment.full_amount_fragments = max_offsets;
                if (!this->node->network()->is_active_connection_exists()) {
                    // eCritical("[Dfs] LoadManager::share_stored_file, no active connections. Cannot share file.
                    // owner_id: {}, file_id: {}, offset: {}", file_link_fragment.file_link.owner_id,
                    // file_link_fragment.file_link.file_id, offset);
                    return;
                }

                Responder responder(nullptr);
                responder.add_identifier(identifier);

                auto message_id = this->node->network()->send_message(file_fragment,
                                                                      MessageType::DfsFileFragment,
                                                                      SendMode::Focused,
                                                                      MessageStatus::NoStatus,
                                                                      responder);
                // eLog("[Dfs] LoadManager::share_stored_file, file fragment sended (message_id: {}). owner_id: {},
                // file_id: {}, offset: {}", message_id, file_link_fragment.file_link.owner_id,
                // file_link_fragment.file_link.file_id, offset);

                // offset += Dfs::Basic::FRAGMENT_SIZE;
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }

            // while (offset < total_size) {

            // }

            // emit node->dfs()->uploaded(file_link_fragment.file_link.owner_id, dir_row.value());
            eLog("instance_dfs share_stored_file out");
        });
    // eLog("[Dfs] LoadManager::share_stored_file, file pushed to waiting send queue. owner_id: {}, file_id: {}",
    // file_link_fragment.file_link.owner_id, file_link_fragment.file_link.file_id);
}

void LoadManager::broadcast_file_exist(const ActorId& owner_id, const std::string& file_id) {
    auto path = Dfs::Path::file_path(owner_id, file_id);
    if (!path.has_value()) {
        return;
    }

    auto dir_row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(), owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }

    // auto size = path->file_size();
    // if (!size.has_value()) {
    //     return;
    // }
    // const uint64_t total_size = size.value();

    if (dir_row->type != Dfs::FileType::File) {
        if (dir_row->type == Dfs::FileType::Collection) {
            node->dfs()->network_request_collection(owner_id, file_id, {});
        }
        if (dir_row->type == Dfs::FileType::Vector) {
            node->dfs()->network_request_vector(owner_id, file_id, {});
        }
        return;
    }

    // static auto calculate_max_offsets = [](size_t total_size, size_t buffer_size) -> size_t {
    //     return (total_size + buffer_size - 1) / buffer_size;
    // };

    // auto max_offsets = calculate_max_offsets(total_size, Dfs::Basic::FRAGMENT_SIZE);

    Dfs::Packets::FileState file_state;
    file_state.file_id           = file_id;
    file_state.owner_id          = owner_id;
    file_state.state             = dir_row->state;
    file_state.hash              = dir_row->hash;
    file_state.notify_neighbours = true;

    auto message_id = this->node->network()->send_message(file_state,
                                                          MessageType::DfsFileExistNotification,
                                                          SendMode::Neighbours,
                                                          MessageStatus::NoStatus);
    // eLog("[Dfs] LoadManager::brodcast_file_exist, file fragment sended (message_id: {}). owner_id: {}, file_id:
    // {}", message_id, owner_id, file_id);
}

void LoadManager::file_fragment_achieved(const Dfs::Packets::FragmentData& file_content,
                                         const std::string&                identifier) {
    Dfs::FileLinkFragment file_link_fragment;
    file_link_fragment.file_link =
        Dfs::FileLink { .owner_id = file_content.owner_id, .file_id = file_content.file_id };
    file_link_fragment.fragment_numbers.emplace(file_content.fragment_number);
    m_amount_file_fragments_requests->erase(file_link_fragment);

    ThreadPoolBoost::instance_dfs()->post([this, file_content, identifier]() {
        eLog("instance_dfs file_fragment_achieved in");
        const auto file_link =
            Dfs::FileLink { .owner_id = file_content.owner_id, .file_id = file_content.file_id };
        // eLog("[Dfs] LoadManager::file_fragment_achieved, achieved fragment to save. file_link: {}, offset: {},
        // fragment_number: {}", file_link, file_content.offset, file_content.fragment_number);

        {
            auto active_reads_locked = *m_active_reads;
            auto item                = active_reads_locked->find(file_link);
            if (item != active_reads_locked->end()) {
                if (item->second.fragments_achieved.contains(file_content.fragment_number)) {
                    // eCritical("[Dfs] LoadManager::file_fragment_achieved, offset already exist. file_link: {},
                    // offset: {}, fragment_number: {}", file_link, file_content.offset,
                    // file_content.fragment_number);
                    eLog("instance_dfs file_fragment_achieved out 1");
                    return;
                }
            } else {
                ReadStorage read_storage { .amount_fragments   = file_content.full_amount_fragments,
                                           .fragments_achieved = {} };
                // read_storage.offsets_read_progress.emplace(file_content.offset, false);

                active_reads_locked->emplace(file_link, read_storage);
            }
        }

        const auto path = Dfs::Path::file_path(file_link.owner_id, file_link.file_id);
        if (!path.has_value()) {
            timer_runner(file_link);
            eLog("instance_dfs file_fragment_achieved out 2");
            return;
        }

        {
            std::lock_guard<std::mutex> m_lock(m_write_file_mutex);
            auto result = Utils::write_file_chunk(path.value(), file_content.data, file_content.offset);
            if (!result.has_value()) {
                // eCritical("[Dfs] LoadManager::file_fragment_achieved, save file to disk error. file_link: {},
                // offset: {}, fragment_number: {}", file_link, file_content.offset, file_content.fragment_number);
                timer_runner(file_link);
                eLog("instance_dfs file_fragment_achieved out 3");
                return;
            }

            auto active_reads_locked = *m_active_reads;
            auto item                = active_reads_locked->find(file_link);
            if (item != active_reads_locked->end())
                item->second.fragments_achieved.emplace(file_content.fragment_number);
        }

        {
            auto active_reads_locked = *m_active_reads;
            auto item                = active_reads_locked->find(file_link);
            if (item != active_reads_locked->end()) {
                auto active_downloads_locked = *m_active_downloads;
                auto res                     = active_downloads_locked->find(file_link);
                if (res != active_downloads_locked->end()) {
                    for (auto& item : res->second.identifier_list) {
                        if (item.first == identifier) {
                            item.second.counter--;
                            break;
                        }
                    }

                    if (item->second.fragments_achieved.size() == item->second.amount_fragments) {
                        active_reads_locked->erase(item);

                        auto dir_row       = res->second.dir_row;
                        bool is_downloaded = node->dfs()->is_file_already_downloaded(file_link.owner_id,
                                                                                     file_link.file_id,
                                                                                     dir_row.hash);
                        if (!is_downloaded) {
                            eLog("[Fragment] Ooops, something wrong. File not downloaded");
                            timer_runner(file_link);
                            eLog("instance_dfs file_fragment_achieved out 4");
                            return;
                        }

                        bool notify_neighbours = res->second.notify_neighbours;

                        active_downloads_locked->erase(res);
                        // eLog("[Dfs] LoadManager::file_fragment_achieved, file downloaded: {}", file_link);

                        finish_him(file_link.owner_id, dir_row);

                        if (notify_neighbours)
                            broadcast_file_exist(file_link.owner_id, file_link.file_id);
                    } else {
                        if (res->second.amount_fragments == 0) {
                            res->second.amount_fragments = file_content.full_amount_fragments;
                            for (int i = 0; i < res->second.amount_fragments; ++i) {
                                if (i + 1 != file_content.fragment_number)
                                    res->second.fragments_left.emplace(i + 1);
                            }
                        } else {
                            res->second.fragments_left.erase(file_content.fragment_number);
                        }
                    }
                    timer_runner(file_link);
                }
            }
        }
        eLog("instance_dfs file_fragment_achieved out");
    });
}

void LoadManager::finish_him(const ActorId& owner_id, const Dfs::DirRow& dir_row) {
    Dfs::Tables::DirsFile::ActorSpace::update_file_state(node->dfs()->get_db_instance(), owner_id, dir_row.file_id, Dfs::FileState::Ready);
    emit node->dfs()->added(owner_id, dir_row);
    emit node->dfs()->downloaded(owner_id, dir_row);
}
