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
#include <QMetaObject>
#include <QThread>
#include <algorithm>

static constexpr qint64 DFS_QUEUE_HIGH_WATER = 4 * 1024 * 1024;
static constexpr qint64 DFS_QUEUE_LOW_WATER  = 2 * 1024 * 1024;

LoadManager::LoadManager(ExtraChainNode* node, QObject* parent)
    : QObject(parent)
    , node(node) {
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        timer_runner();
        schedule_watchdog();
    });
    connect(node, &ExtraChainNode::runtimeActivityChanged, this, [this](RuntimeActivity activity) {
        if (activity == RuntimeActivity::Background) {
            m_timer->stop();
            return;
        }
        timer_runner();
        schedule_watchdog();
    });
}

std::size_t LoadManager::max_concurrent_downloads() const {
    return node->runtime_limits().dfs_downloads;
}

void LoadManager::schedule_watchdog() {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this,
            [this]() {
                schedule_watchdog();
            },
            Qt::QueuedConnection);
        return;
    }

    if (node->runtime_activity() == RuntimeActivity::Background || max_concurrent_downloads() == 0
        || (m_active_downloads->empty() && m_active_downloads_priority->empty())) {
        m_timer->stop();
        return;
    }

    if (!m_timer->isActive()) {
        m_timer->start(5000);
    }
}

void LoadManager::timer_runner(const Dfs::FileLink file_link_to_proceed) {
    const auto download_limit = max_concurrent_downloads();
    if (download_limit == 0) {
        return;
    }
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
        if (!active_downloads->empty() && m_amount_file_fragments_requests->size() < download_limit) {
            auto active_downloads_locked = *active_downloads;
            for (int pass = 0; pass < 2; ++pass) {
                for (auto& [file_link, load_info] : *active_downloads_locked) {
                    if ((pass == 0) != load_info.forced) {
                        continue;
                    }
                    if (m_amount_file_fragments_requests->size() >= download_limit)
                        return false;

                    bool ignore_timeout     = file_link_to_proceed == file_link;
                    bool is_requested       = false;
                    bool has_pending_source = false;

                    // Remove disconnected identifiers from the list
                    load_info.identifier_list
                        .erase(std::remove_if(load_info.identifier_list.begin(),
                                              load_info.identifier_list.end(),
                                              [this](const auto& id_pair) {
                                                  return !node->network()->is_connection_exists(id_pair.first);
                                              }),
                               load_info.identifier_list.end());

                    // If no identifiers left, try to find new peers who have this file
                    if (load_info.identifier_list.empty()) {
                        eLog("[LoadManager] No active identifiers for file {}, asking neighbours",
                             file_link.file_id);
                        load_info.identifier_storage_checker.clear();

                        // Add all active connections as potential sources
                        auto connections_locked = *node->network()->connections();
                        for (const auto& socket : *connections_locked) {
                            if (!socket || !socket->is_active())
                                continue;
                            auto conn_id = socket->identifier().toStdString();
                            if (conn_id.empty())
                                continue;
                            if (!load_info.identifier_storage_checker.contains(conn_id)) {
                                load_info.identifier_storage_checker.emplace(conn_id);
                                load_info.identifier_list.emplace_back(conn_id,
                                                                       LoadInfo::Attempts { .counter = 0 });
                            }
                        }

                        // Keep the item queued until a source becomes available.
                        if (load_info.identifier_list.empty()) {
                            eLog("[LoadManager] No connections available for file {}", file_link.file_id);
                            continue;
                        }
                    }

                    std::stable_sort(load_info.identifier_list.begin(),
                                     load_info.identifier_list.end(),
                                     [](const auto& left, const auto& right) {
                                         return left.second.counter < right.second.counter;
                                     });

                    for (auto& identifier : load_info.identifier_list) {
                        if (m_amount_file_fragments_requests->size() >= download_limit)
                            return false;

                        auto now      = std::chrono::system_clock::now();
                        auto duration = now - identifier.second.last_attempt;
                        if (!node->network()->is_connection_exists(identifier.first)) {
                            // eCritical(
                            //     "LoadManager::timer_runner, connection with identifier ({}) not exist for
                            //     file_link:
                            //     {}.", identifier.first, it.first);
                            continue;
                        }

                        if (identifier.second.counter >= 3)
                            continue;
                        else if (identifier.second.counter == 0
                                 || (duration > std::chrono::seconds(10) || ignore_timeout)) {
                            if (identifier.second.counter == 1 && load_info.identifier_list.size() == 1) {
                                this->node->network()->send_message(file_link,
                                                                    MessageType::DfsFileRequestContinueUpload,
                                                                    SendMode::Neighbours,
                                                                    MessageStatus::Request);
                            }

                            identifier.second.counter++;
                            identifier.second.last_attempt = std::chrono::system_clock::now();

                            Responder responder(nullptr);
                            responder.add_identifier(identifier.first);

                            Dfs::FileLinkFragment output;
                            output.file_link = file_link;

                            bool is_setted = false;

                            if (load_info.amount_fragments > 0) {
                                for (auto number : load_info.fragments_left) {
                                    if (m_amount_file_fragments_requests->size() >= download_limit)
                                        break;
                                    output.fragment_numbers.emplace(number);
                                    Dfs::FileLinkFragment pending_fragment;
                                    pending_fragment.file_link = file_link;
                                    pending_fragment.fragment_numbers.emplace(number);
                                    m_amount_file_fragments_requests->emplace(pending_fragment,
                                                                              std::chrono::system_clock::now());
                                    is_setted = true;
                                }
                            } else {
                                output.fragment_numbers.emplace(1);
                                m_amount_file_fragments_requests->emplace(output,
                                                                          std::chrono::system_clock::now());
                                is_setted = true;
                            }

                            if (is_setted) {
                                this->node->network()->send_message(output,
                                                                    MessageType::DfsFileRequest,
                                                                    SendMode::Focused,
                                                                    MessageStatus::NoStatus,
                                                                    responder.with_new_message_id());

                                // eLog("LoadManager::timer_runner, try to send request once more with identifier
                                // ({}), attempt: {} for file_link: {} and fragments: {}.", identifier.first,
                                // identifier.second.counter, it.first, output.fragment_numbers);
                                is_requested = true;
                                break;
                            }
                        } else {
                            has_pending_source = true;
                            if (!load_info.forced) {
                                break;
                            }
                        }
                    }
                    auto identifier_list_size = load_info.identifier_list.size();
                    if (!is_requested && !has_pending_source && identifier_list_size > 0) {
                        load_info.identifier_list.clear();
                        load_info.identifier_storage_checker.clear();
                    }
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
    m_active_downloads_priority->erase(file_link_fragment.file_link);
    m_active_downloads->erase(file_link_fragment.file_link);
    m_amount_file_fragments_requests->erase(file_link_fragment);
    schedule_watchdog();
    // eLog("m_active_downloads{}->erase: {}", is_priority ? "_priority" : "",
    // file_link_fragment.file_link.hash());
}

bool LoadManager::add_node_identifier(const Dfs::FileLink& file_link, std::string identifier) {
    auto process_func = [&file_link,
                         &identifier](SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& active_downloads,
                                      bool                                                  is_priority) {
        auto active_downloads_locked = *active_downloads;
        auto it                      = active_downloads_locked->find(file_link);
        if (it != active_downloads_locked->end()) {
            auto identifier_storage_checker_it = it->second.identifier_storage_checker.emplace(identifier);
            if (identifier_storage_checker_it.second) {
                it->second.identifier_list.emplace_back(identifier, LoadInfo::Attempts { .counter = 0 });
                eLog("m_active_downloads{} update list: {} || {}",
                     is_priority ? "_priority" : "",
                     file_link.hash(),
                     identifier);
                return true;
            }
        }
        return false;
    };

    if (process_func(m_active_downloads_priority, true)) {
        return true;
    }
    return process_func(m_active_downloads, false);
}

void LoadManager::add_to_queue(const ActorId&     owner_id,
                               const Dfs::DirRow& dir_row,
                               const std::string& identifier,
                               const bool         notify_neighbours) {
    auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = dir_row.file_id };
    bool is_forced = node->dfs()->forces_files_.contains(file_link);

    bool is_priority = node->dfs()->is_priority(file_link) || is_forced;

    if (!node_enabled.load()) {
        return;
    }

    if (dir_row.type == Dfs::FileType::Folder) {
        return;
    }

    bool is_full = node->dfs()->mode() == DfsMode::Full;

    const auto update_info = [&](LoadInfo& info) {
        info.forced = info.forced || is_forced;
        if (!identifier.empty() && info.identifier_storage_checker.emplace(identifier).second) {
            info.identifier_list.emplace_back(identifier, LoadInfo::Attempts { .counter = 0 });
        }
    };

    bool found_existing = false;
    {
        auto priority_locked   = *m_active_downloads_priority;
        auto priority_existing = priority_locked->find(file_link);
        if (priority_existing != priority_locked->end()) {
            update_info(priority_existing->second);
            found_existing = true;
        } else if (is_priority) {
            auto normal_locked   = *m_active_downloads;
            auto normal_existing = normal_locked->find(file_link);
            if (normal_existing != normal_locked->end()) {
                update_info(normal_existing->second);
                priority_locked->emplace(file_link, std::move(normal_existing->second));
                normal_locked->erase(normal_existing);
                found_existing = true;
            }
        }
    }

    if (!found_existing && !is_priority) {
        auto normal_locked   = *m_active_downloads;
        auto normal_existing = normal_locked->find(file_link);
        if (normal_existing != normal_locked->end()) {
            update_info(normal_existing->second);
            found_existing = true;
        }
    }

    if (found_existing) {
        if (is_forced) {
            node->dfs()->forces_files_.erase(file_link);
            timer_runner(file_link);
            schedule_watchdog();
        }
        return;
    }

    if (is_forced) {
        node->dfs()->forces_files_.erase(file_link);
    }

    bool need_load = is_full || node->dfs()->is_priority(owner_id) || is_forced;

    if (/*dir_row.type == Dfs::FileType::File && (dir_row.state != Dfs::FileState::Ready ||*/ !need_load /*)*/) {
        return;
    }

    auto row =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(), owner_id, dir_row.file_id);
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

            if ((row->type == Dfs::FileType::Vector || row->type == Dfs::FileType::Dictionary)
                && file_path->exists() && row->hash == dir_row.hash) {
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

    try {
        std::filesystem::create_directories(fmt::format("{}/{}", DfsB::DFS_FOLDER, owner_id));
    } catch (const std::exception& e) {
        eWarning("[LoadManager] Failed to create directory: {}", e.what());
        return;
    }

    auto load_info = LoadInfo { .dir_row = dir_row };
    // LoadInfo::Attempts attempts { .counter = 1, .last_attempt = std::chrono::system_clock::now()};
    LoadInfo::Attempts attempts { .counter = 0 };
    if (!identifier.empty()) {
        load_info.identifier_storage_checker.emplace(identifier);
        load_info.identifier_list.emplace_back(identifier, attempts);
    }

    load_info.dir_row.state = Dfs::FileState::Known;

    load_info.notify_neighbours = notify_neighbours;
    load_info.forced            = is_forced;

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
        eLog("m_active_downloads{}->emplace: {}", is_priority ? "_priority" : "", file_link);
        timer_runner(file_link);
        schedule_watchdog();
    } else {
        // eWarning("LoadManager::add_to_queue, file_link exist: {}. Adding identifier to the list...", file_link);
        add_node_identifier(file_link, identifier);
    }
}

void LoadManager::add_to_queue(const ActorId&                  owner_id,
                               const std::vector<Dfs::DirRow>& dir_rows,
                               const std::string&              identifier) {
    if (dir_rows.empty()) {
        return;
    }

    bool is_full = node->dfs()->mode() == DfsMode::Full;

    for (const auto& dir_row : dir_rows) {
        if (dir_row.state == Dfs::FileState::Removed) {
            continue;
        }

        auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = dir_row.file_id };
        // request_file can run before this actor's directory arrives. A forced
        // file must enter the queue when its directory row becomes available.
        bool need_load =
            is_full || node->dfs()->is_priority(file_link) || node->dfs()->forces_files_.contains(file_link);
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

    auto dir_row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(),
                                                                  file_link_fragment.file_link.owner_id,
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
        if (dir_row->type == Dfs::FileType::Folder) {
            return;
        }
        if (dir_row->type == Dfs::FileType::Collection) {
            node->dfs()->network_request_collection(file_link_fragment.file_link.owner_id,
                                                    file_link_fragment.file_link.file_id,
                                                    responder);
            // eCritical("LoadManager::share_stored_file, its a collection. Another thlow. file_id: {}",
            // file_link_fragment.file_link.file_id);
        }
        if (dir_row->type == Dfs::FileType::Vector || dir_row->type == Dfs::FileType::Dictionary) {
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

                int  progress = static_cast<int>((fragment_number * 100) / max_offsets);
                emit node->dfs()->uploadProgress(file_link_fragment.file_link.owner_id,
                                                 file_link_fragment.file_link.file_id,
                                                 progress);

                // SocketService already serializes writes. Pause only when its
                // bounded peer queue reaches the high-water mark instead of
                // limiting every fragment to a fixed 100 ms delay.
                if (node->network()->connection_pending_bytes(identifier) > DFS_QUEUE_HIGH_WATER) {
                    while (node->network()->is_connection_exists(identifier)
                           && node->network()->connection_pending_bytes(identifier) > DFS_QUEUE_LOW_WATER) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                }
            }

            emit node->dfs()->uploadProgress(file_link_fragment.file_link.owner_id,
                                             file_link_fragment.file_link.file_id,
                                             100);
        });
    // eLog("[Dfs] LoadManager::share_stored_file, file pushed to waiting send queue. owner_id: {}, file_id: {}",
    // file_link_fragment.file_link.owner_id, file_link_fragment.file_link.file_id);
}

void LoadManager::broadcast_file_exist(const ActorId& owner_id, const std::string& file_id) {
    auto path = Dfs::Path::file_path(owner_id, file_id);
    if (!path.has_value()) {
        return;
    }

    auto dir_row =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(), owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }

    // auto size = path->file_size();
    // if (!size.has_value()) {
    //     return;
    // }
    // const uint64_t total_size = size.value();

    if (dir_row->type != Dfs::FileType::File) {
        if (dir_row->type == Dfs::FileType::Folder) {
            return;
        }
        if (dir_row->type == Dfs::FileType::Collection) {
            node->dfs()->network_request_collection(owner_id, file_id, {});
        }
        if (dir_row->type == Dfs::FileType::Vector || dir_row->type == Dfs::FileType::Dictionary) {
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
            return;
        }

        {
            auto&                       write_mutex = m_write_file_mutexes[file_link.hash() % WRITE_STRIPES];
            std::lock_guard<std::mutex> m_lock(write_mutex);
            auto result = Utils::write_file_chunk(path.value(), file_content.data, file_content.offset);
            if (!result.has_value()) {
                // eCritical("[Dfs] LoadManager::file_fragment_achieved, save file to disk error. file_link: {},
                // offset: {}, fragment_number: {}", file_link, file_content.offset, file_content.fragment_number);
                timer_runner(file_link);
                return;
            }

            auto active_reads_locked = *m_active_reads;
            auto item                = active_reads_locked->find(file_link);
            if (item != active_reads_locked->end()) {
                item->second.fragments_achieved.emplace(file_content.fragment_number);
            }
        }

        bool download_complete = false;
        {
            auto active_reads_locked = *m_active_reads;
            auto item                = active_reads_locked->find(file_link);
            if (item == active_reads_locked->end()) {
                return;
            }
            download_complete = item->second.fragments_achieved.size() == item->second.amount_fragments;
            if (download_complete) {
                active_reads_locked->erase(item);
            }
        }

        Dfs::DirRow completed_row;
        bool        notify_neighbours = false;
        const auto  update_download = [&](SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& active_downloads) {
            auto active_downloads_locked = *active_downloads;
            auto item                    = active_downloads_locked->find(file_link);
            if (item == active_downloads_locked->end()) {
                return false;
            }

            for (auto& source : item->second.identifier_list) {
                if (source.first == identifier) {
                    if (source.second.counter > 0) {
                        source.second.counter--;
                    }
                    break;
                }
            }

            if (download_complete) {
                completed_row     = item->second.dir_row;
                notify_neighbours = item->second.notify_neighbours;
            } else {
                item->second.last_fragment_received = std::chrono::system_clock::now();
                if (item->second.amount_fragments == 0) {
                    item->second.amount_fragments = file_content.full_amount_fragments;
                    for (int number = 1; number <= item->second.amount_fragments; ++number) {
                        if (number != file_content.fragment_number) {
                            item->second.fragments_left.emplace(number);
                        }
                    }
                } else {
                    item->second.fragments_left.erase(file_content.fragment_number);
                }
            }
            return true;
        };

        const bool found_download =
            update_download(m_active_downloads_priority) || update_download(m_active_downloads);
        if (!found_download) {
            return;
        }

        if (!download_complete) {
            timer_runner(file_link);
            return;
        }

        const bool is_downloaded =
            node->dfs()->is_file_already_downloaded(file_link.owner_id, file_link.file_id, completed_row.hash);
        if (!is_downloaded) {
            eLog("[Fragment] File validation failed. File link: {}", file_link);
            const auto reset_download = [&](SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& downloads) {
                auto locked = *downloads;
                auto item   = locked->find(file_link);
                if (item == locked->end()) {
                    return false;
                }
                item->second.amount_fragments = 0;
                item->second.fragments_left.clear();
                for (auto& source : item->second.identifier_list) {
                    source.second.counter = 0;
                }
                return true;
            };
            if (!reset_download(m_active_downloads_priority)) {
                reset_download(m_active_downloads);
            }
            timer_runner(file_link);
            return;
        }

        Dfs::FileLinkFragment completed_fragment;
        completed_fragment.file_link = file_link;
        completed_fragment.fragment_numbers.emplace(file_content.fragment_number);
        remove_active_download(completed_fragment);
        finish_him(file_link.owner_id, completed_row);
        if (notify_neighbours) {
            broadcast_file_exist(file_link.owner_id, file_link.file_id);
        }
        timer_runner();
    });
}

void LoadManager::finish_him(const ActorId& owner_id, const Dfs::DirRow& dir_row) {
    eLog("[LoadManager] Finish {} / {}", owner_id, dir_row.file_id);

    Dfs::Tables::DirsFile::ActorSpace::update_file_state(node->dfs()->get_db_instance(),
                                                         owner_id,
                                                         dir_row.file_id,
                                                         Dfs::FileState::Ready);
    emit node->dfs()->added(owner_id, dir_row);
    emit node->dfs()->downloaded(owner_id, dir_row);
}

bool LoadManager::is_downloading(const Dfs::FileLink& file_link) const {
    constexpr auto ACTIVITY_TIMEOUT = std::chrono::seconds(60);
    auto           now              = std::chrono::system_clock::now();

    auto check_active = [&](const SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& downloads) -> bool {
        auto locked = *downloads;
        auto it     = locked->find(file_link);
        if (it == locked->end()) {
            return false;
        }

        // Check if fragment was received within last minute
        if (it->second.last_fragment_received.time_since_epoch().count() > 0) {
            auto elapsed = now - it->second.last_fragment_received;
            if (elapsed < ACTIVITY_TIMEOUT) {
                return true;
            }
        }

        return false;
    };

    if (check_active(m_active_downloads_priority)) {
        return true;
    }

    return check_active(m_active_downloads);
}
