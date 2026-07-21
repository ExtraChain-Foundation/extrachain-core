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
#include <algorithm>
#include <optional>

LoadManager::LoadManager(ExtraChainNode* node, QObject* parent)
    : QObject(parent)
    , node(node) {
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, [this]() {
        timer_runner();
    });
    m_timer->start(5000);
}

void LoadManager::kick() {
    if (kick_pending_.exchange(true)) {
        return;
    }
    QMetaObject::invokeMethod(
        this,
        [this] {
            kick_pending_.store(false);
            if (!m_timer->isActive()) {
                m_timer->start(5000);
            }
            timer_runner();
        },
        Qt::QueuedConnection);
}

bool LoadManager::compute_vectors_waiting() {
    const auto now = std::chrono::system_clock::now();
    // Copy under a short lock: holding m_completed_once over the pool locks deadlocks — finish_him takes them in reverse order.
    std::set<Dfs::FileLink> completed_once;
    {
        auto locked    = *m_completed_once;
        completed_once = *locked;
    }
    auto has_fresh = [&](SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& pool) {
        auto locked = *pool;
        for (const auto& [link, info] : *locked) {
            if (node->dfs()->download_rank(link.owner_id, info.dir_row) > DfsController::RANK_OTHER_VECTORS) {
                continue;
            }
            // Re-downloading an already-completed vector (hash-mismatch cycle) doesn't hold the gate — else files would starve forever.
            if (completed_once.contains(link)) {
                continue;
            }
            if (now - info.queued < std::chrono::seconds(60)
                || now - info.last_fragment_received < std::chrono::seconds(15)) {
                return true;
            }
        }
        return false;
    };
    return has_fresh(m_active_downloads_priority) || has_fresh(m_active_downloads);
}

void LoadManager::timer_runner(const Dfs::FileLink file_link_to_proceed) {
    {
        auto amount_file_fragments_requests_locked = *m_amount_file_fragments_requests;
        auto now                                   = std::chrono::system_clock::now();
        for (auto it = amount_file_fragments_requests_locked->begin();
             it != amount_file_fragments_requests_locked->end();) {
            auto duration = now - it->second;
            // 4s (was 10s): a dead request held the slot, and with slots full the
            // scheduler stalled entirely — one dead peer used to cost 10s.
            if (duration > std::chrono::seconds(4))
                it = amount_file_fragments_requests_locked->erase(it);
            else
                ++it;
        }
    }

    // "Vectors before files" gate: skip file scheduling while a vector (rank<=4) can still
    // download. Freshness window guards against starving files forever if no peer has the vector.
    // Computed lazily: on the per-fragment targeted path this full two-map scan is
    // usually never needed — that matters on phones where the path runs per arrival.
    std::optional<bool>       vectors_waiting_cache;
    const auto vectors_waiting = [&]() -> bool {
        if (vectors_waiting_cache.has_value()) {
            return *vectors_waiting_cache;
        }
        vectors_waiting_cache = compute_vectors_waiting();
        // Log the gate flipping: when files get paused and when they're released.
        static bool last_vectors_waiting = false;
        if (*vectors_waiting_cache != last_vectors_waiting) {
            last_vectors_waiting = *vectors_waiting_cache;
            eLog("[Load] {}", *vectors_waiting_cache ? "files PAUSED (vectors downloading)" : "files RESUMED");
        }
        return *vectors_waiting_cache;
    };

    auto process_func = [&](SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& active_downloads) -> bool {
        if (!active_downloads->empty()) {
            auto active_downloads_locked = *active_downloads;
            std::vector<Dfs::FileLink> download_order;

            // Targeted per-fragment call: touch only the file that just received a
            // fragment. The full sorted pass over every transfer (plus the gate
            // scan) runs on the periodic timer — not once per arriving fragment.
            if (!file_link_to_proceed.file_id.empty()) {
                if (active_downloads_locked->contains(file_link_to_proceed)) {
                    download_order.emplace_back(file_link_to_proceed);
                }
                // fall through with a possibly empty order — nothing else to do here
            } else {
            download_order.reserve(active_downloads_locked->size());
            for (const auto& item : *active_downloads_locked) {
                download_order.emplace_back(item.first);
            }

            // Order: 0 network vectors -> 1 raccoon files -> 2 chat-actor vectors ->
            // 3 main-actor vectors -> 4 other vectors -> 5 files; within a rank, forced
            // (explicit request_file) go first.
            auto rank_of = [&](const Dfs::FileLink& link) {
                const auto it = active_downloads_locked->find(link);
                return it != active_downloads_locked->end()
                           ? node->dfs()->download_rank(link.owner_id, it->second.dir_row)
                           : 5;
            };
            std::stable_sort(download_order.begin(),
                             download_order.end(),
                             [&](const Dfs::FileLink& lhs, const Dfs::FileLink& rhs) {
                                 const int lhs_rank = rank_of(lhs);
                                 const int rhs_rank = rank_of(rhs);
                                 if (lhs_rank != rhs_rank) {
                                     return lhs_rank < rhs_rank;
                                 }
                                 const auto lhs_it = active_downloads_locked->find(lhs);
                                 const auto rhs_it = active_downloads_locked->find(rhs);
                                 const bool lhs_forced =
                                     lhs_it != active_downloads_locked->end() && lhs_it->second.forced;
                                 const bool rhs_forced =
                                     rhs_it != active_downloads_locked->end() && rhs_it->second.forced;
                                 return lhs_forced && !rhs_forced;
                             });
            }

            for (const auto& file_link : download_order) {
                auto it = active_downloads_locked->find(file_link);
                if (it == active_downloads_locked->end()) {
                    continue;
                }

                auto& load_info = it->second;

                // Files stay paused while vectors are downloading. Forced files (explicit
                // user request_file, e.g. tapping media) are not paused.
                if (!load_info.forced
                    && node->dfs()->download_rank(file_link.owner_id, load_info.dir_row) > DfsController::RANK_OTHER_VECTORS
                    && vectors_waiting()) {
                    continue;
                }
                bool  ignore_timeout = file_link_to_proceed == file_link;
                bool  is_requested   = false;
                const size_t active_request_limit =
                    load_info.forced ? MAX_CONCURRENT_DOWNLOADS + MAX_FORCED_DOWNLOADS
                                     : MAX_CONCURRENT_DOWNLOADS;

                if (m_amount_file_fragments_requests->size() >= active_request_limit)
                    return false;

                // Remove disconnected identifiers from the list
                load_info.identifier_list.erase(
                    std::remove_if(load_info.identifier_list.begin(),
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
                            load_info.identifier_list.emplace_back(conn_id, LoadInfo::Attempts { .counter = 0 });
                        }
                    }

                    // If still no identifiers, remove from queue
                    if (load_info.identifier_list.empty()) {
                        eLog("[LoadManager] No connections available for file {}, removing from queue",
                             file_link.file_id);
                        active_downloads_locked->erase(it);
                        continue;
                    }
                }

                for (auto& identifier : load_info.identifier_list) {
                    if (m_amount_file_fragments_requests->size() >= active_request_limit)
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
                    // Retry to the same peer — 4s (was 10s), paired with the slot timeout.
                    else if (identifier.second.counter == 0
                             || (duration > std::chrono::seconds(4) || ignore_timeout)) {
                        if (identifier.second.counter == 1 && load_info.identifier_list.size() == 1) {
                            this->node->network()->send_message(file_link,
                                                                MessageType::DfsFileRequestContinueUpload,
                                                                SendMode::Neighbours,
                                                                MessageStatus::Request);
                        }

                        Responder responder(nullptr);
                        responder.add_identifier(identifier.first);

                        Dfs::FileLinkFragment output;
                        output.file_link = file_link;

                        bool is_setted     = false;
                        bool all_in_flight = false;

                        if (it->second.amount_fragments > 0) {
                            // True also covers the tail state where fragments_left is
                            // already empty: everything received, the pool is still
                            // writing/finalizing — nothing to request, but the transfer
                            // is alive and must not fall into the give-up path.
                            all_in_flight = true;
                            for (auto number : it->second.fragments_left) {
                                if (m_amount_file_fragments_requests->size() >= active_request_limit)
                                    break;
                                // Slot bookkeeping is per single fragment. A cumulative key
                                // ({1},{1,2},{1,2,3}...) is only erased by the 4s purge, never
                                // by fragment arrival, so it pins a slot and stalls the pipeline.
                                Dfs::FileLinkFragment single;
                                single.file_link = file_link;
                                single.fragment_numbers.emplace(number);
                                if (!m_amount_file_fragments_requests
                                         ->emplace(single, std::chrono::system_clock::now())
                                         .second) {
                                    continue; // already in flight
                                }
                                output.fragment_numbers.emplace(number);
                                is_setted     = true;
                                all_in_flight = false;
                            }
                        } else {
                            output.fragment_numbers.emplace(1);
                            m_amount_file_fragments_requests->emplace(output, std::chrono::system_clock::now());
                            is_setted = true;
                        }

                        if (is_setted)
                        {
                            // The attempt counter only moves when a request actually
                            // goes out — an idle pass (window full, tail finalizing)
                            // must not age the source towards the dead-peer limit.
                            identifier.second.counter++;
                            identifier.second.last_attempt = std::chrono::system_clock::now();
                            this->node->network()->send_message(output,
                                                                MessageType::DfsFileRequest,
                                                                SendMode::Focused,
                                                                MessageStatus::NoStatus,
                                                                responder.with_new_message_id());

                           // eLog("LoadManager::timer_runner, try to send request once more with identifier ({}),
                           // attempt: {} for file_link: {} and fragments: {}.", identifier.first,
                           // identifier.second.counter, it.first, output.fragment_numbers);
                            is_requested = true;
                            break;
                        }
                        if (all_in_flight) {
                            // Everything left is already requested and waiting — the
                            // transfer is alive, don't burn a source-refresh cycle.
                            is_requested = true;
                            break;
                        }
                    } else if (load_info.forced) {
                        // An urgent transfer must not wait behind a cooling-down
                        // source while other confirmed sources are available.
                        continue;
                    } else {
                        is_requested = true;
                        break;
                    }
                }
                auto identifier_list_size = load_info.identifier_list.size();
                if (!is_requested && identifier_list_size > 0) {
                    if (++load_info.source_refresh_cycles > 3) {
                        eLog("[Load] GIVE UP {}/{} after {} source cycles",
                             file_link.owner_id,
                             file_link.file_id,
                             load_info.source_refresh_cycles);
                        active_downloads_locked->erase(it);
                        continue;
                    }
                    eLog("[LoadManager] Exhausted identifiers for file {}, refreshing sources", file_link.file_id);
                    load_info.identifier_list.clear();
                    load_info.identifier_storage_checker.clear();
                }
            }
            return true;
        }
        return true;
    };

    if (process_func(m_active_downloads_priority))
        process_func(m_active_downloads);

    // Idle: nothing queued in either pool — stop the periodic tick so an idle
    // messenger does not wake the CPU every 5 seconds (battery on phones).
    // kick() re-arms the timer when a download is queued again.
    if (file_link_to_proceed.file_id.empty() && m_active_downloads->empty()
        && m_active_downloads_priority->empty()) {
        QMetaObject::invokeMethod(
            this,
            [this] {
                if (m_active_downloads->empty() && m_active_downloads_priority->empty()) {
                    m_timer->stop();
                }
            },
            Qt::QueuedConnection);
    }
}

void LoadManager::remove_active_download(const Dfs::FileLinkFragment& file_link_fragment) {
    m_active_downloads_priority->erase(file_link_fragment.file_link);
    m_active_downloads->erase(file_link_fragment.file_link);
    m_amount_file_fragments_requests->erase(file_link_fragment);
}

bool LoadManager::add_node_identifier(const Dfs::FileLink& file_link, std::string identifier) {
    auto process_func = [&file_link, &identifier](
                            SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& active_downloads,
                            bool                                                   is_priority) {
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

    auto update_existing = [&](SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& active_downloads) {
        auto active_downloads_locked = *active_downloads;
        auto existing                = active_downloads_locked->find(file_link);
        if (existing == active_downloads_locked->end()) {
            return false;
        }

        if (is_forced) {
            existing->second.forced = true;
        }
        if (!identifier.empty()
            && existing->second.identifier_storage_checker.emplace(identifier).second) {
            existing->second.identifier_list.emplace_back(identifier, LoadInfo::Attempts { .counter = 0 });
        }
        return true;
    };

    // A repeated forced request must promote the existing transfer instead of
    // discarding its source selection, retry counters and partial progress.
    if (update_existing(m_active_downloads_priority) || update_existing(m_active_downloads)) {
        if (is_forced) {
            node->dfs()->forces_files_.erase(file_link);
            QMetaObject::invokeMethod(this,
                                      [this, file_link]() {
                                          timer_runner(file_link);
                                      },
                                      Qt::QueuedConnection);
        }
        return;
    }

    if (is_forced) {
        node->dfs()->forces_files_.erase(file_link);
    }

    bool need_load = is_full || node->dfs()->is_priority(file_link) || is_forced;

    if (/*dir_row.type == Dfs::FileType::File && (dir_row.state != Dfs::FileState::Ready ||*/ !need_load /*)*/) {
        return;
    }

    auto row =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(), owner_id, dir_row.file_id);

    // Vector unreadable on disk (missing DB or .vector template companion) while .dirs says
    // Ready with the correct hash — state left by an interrupted write (kill during sync).
    // All early-returns below used to permanently block re-download. Don't skip: the
    // content-package will restore both files.
    bool vector_broken_on_disk = false;
    if (row.has_value() && row->type == Dfs::FileType::Vector) {
        const auto main_path = std::filesystem::path(Dfs::Path::filePath(owner_id, dir_row.file_id));
        vector_broken_on_disk =
            !std::filesystem::exists(main_path) || !std::filesystem::exists(main_path.string() + ".vector");
        if (vector_broken_on_disk) {
            eWarning("[Load] REPAIR {}/{}: vector unreadable on disk (db or .vector missing)",
                     owner_id,
                     dir_row.file_id);
        }
    }

    if (row.has_value() && !vector_broken_on_disk) {
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
    if (!vector_broken_on_disk
        && node->dfs()->is_file_already_downloaded(owner_id, dir_row.file_id, dir_row.hash)) {
        return;
    }

    // eLog("Adding file to download queue: {} / {}", owner_id, dir_row);

    try {
        std::filesystem::create_directories(fmt::format("{}/{}", DfsB::DFS_FOLDER, owner_id));
    } catch (const std::exception& e) {
        eWarning("[LoadManager] Failed to create directory: {}", e.what());
        return;
    }

    auto load_info   = LoadInfo { .dir_row = dir_row };
    load_info.queued = std::chrono::system_clock::now();
    // LoadInfo::Attempts attempts { .counter = 1, .last_attempt = std::chrono::system_clock::now()};
    LoadInfo::Attempts attempts { .counter = 0 };
    load_info.identifier_storage_checker.emplace(identifier);
    load_info.identifier_list.emplace_back(identifier, attempts);

    if (is_forced) {
        auto connections_locked = *node->network()->connections();
        for (const auto& socket : *connections_locked) {
            if (!socket || !socket->is_active()) {
                continue;
            }

            const auto conn_id = socket->identifier().toStdString();
            if (conn_id.empty() || load_info.identifier_storage_checker.contains(conn_id)) {
                continue;
            }

            load_info.identifier_storage_checker.emplace(conn_id);
            load_info.identifier_list.emplace_back(conn_id, attempts);
        }
    }

    // The stored size is known from the dir row, so the fragment map can be
    // seeded upfront: the first scheduler pass fills the whole request window
    // instead of fetching fragment 1 alone and waiting a round-trip to learn
    // the count.
    if (dir_row.type == Dfs::FileType::File && dir_row.size > 0) {
        load_info.amount_fragments =
            (dir_row.size + Dfs::Basic::FRAGMENT_SIZE - 1) / Dfs::Basic::FRAGMENT_SIZE;
        for (size_t n = 1; n <= load_info.amount_fragments; ++n) {
            load_info.fragments_left.emplace(n);
        }
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
        eLog("[Load] QUEUE rank={} {} {}/{} size={} hash={} lm={}{}{}",
             node->dfs()->download_rank(owner_id, dir_row),
             dir_row.folder.value_or("-"),
             owner_id,
             dir_row.file_id,
             dir_row.size,
             dir_row.hash.substr(0, 8),
             dir_row.last_modified,
             is_forced ? " forced" : "",
             is_priority ? " prio-pool" : "");
        kick();
        // Responder responder(nullptr);
        // responder.add_identifier(identifier);
        // this->node->network()->send_message(file_link,
        //         MessageType::DfsFileRequest,
        //         SendMode::Focused,
        //         MessageStatus::NoStatus,
        //         responder);
        eLog("m_active_downloads{}->emplace: {}", is_priority ? "_priority" : "", file_link);
        if (load_info.forced) {
            QMetaObject::invokeMethod(this,
                                      [this, file_link]() {
                                          timer_runner(file_link);
                                      },
                                      Qt::QueuedConnection);
        }
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
        // forces_files_: request_file could arrive BEFORE this actor's dirs synced (file
        // from another user after a from-scratch import) — the newly arrived dir_row is
        // itself the signal it's now downloadable. Otherwise the file would wait for
        // another request_file (30s throttle plus another UI request).
        bool need_load = is_full || node->dfs()->is_priority(file_link)
                         || node->dfs()->forces_files_.contains(file_link);
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

                int progress = static_cast<int>((fragment_number * 100) / max_offsets);
                emit node->dfs()->uploadProgress(file_link_fragment.file_link.owner_id,
                                                  file_link_fragment.file_link.file_id,
                                                  progress);
                // No pacing sleep: a request carries at most the requester's
                // slot-window of fragments, so the requester already flow-controls.
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

    // Mark the fragment as received synchronously: the disk write runs on a
    // single-thread pool, and while it lags a scheduler refill would re-request
    // fragments that are already here — every duplicate is a re-sent 250KB.
    {
        auto mark_received = [&](SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& active_downloads) {
            auto locked = *active_downloads;
            auto res    = locked->find(file_link_fragment.file_link);
            if (res == locked->end()) {
                return false;
            }
            res->second.fragments_left.erase(file_content.fragment_number);
            res->second.last_fragment_received = std::chrono::system_clock::now();
            // Attempt bookkeeping lives here too: if it lagged behind on the
            // pool, the retry counter would climb over its limit and gate the
            // refills sent below.
            for (auto& id_pair : res->second.identifier_list) {
                if (id_pair.first == identifier) {
                    // Clamp at zero: one request carries a whole window of
                    // fragments, so per-fragment decrements would drive the
                    // counter negative and fail the `counter == 0` send gate —
                    // a forced file then falls through to GIVE UP at the tail.
                    if (id_pair.second.counter > 0) {
                        id_pair.second.counter--;
                    }
                    break;
                }
            }
            return true;
        };
        if (mark_received(m_active_downloads_priority) || mark_received(m_active_downloads)) {
            // Refill the request window right away: the dfs pool that runs the
            // disk write below can be busy with vector handling for seconds,
            // and by then the retry timeout has already stalled the transfer.
            timer_runner(file_link_fragment.file_link);
        }
    }

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
            std::lock_guard<std::mutex> m_lock(m_write_file_mutex);
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

        {
            auto active_reads_locked = *m_active_reads;
            auto item                = active_reads_locked->find(file_link);
            if (item != active_reads_locked->end()) {
                auto process_func = [&](SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& active_downloads) {
                    auto active_downloads_locked = *active_downloads;
                    auto res                     = active_downloads_locked->find(file_link);
                    if (res == active_downloads_locked->end()) {
                        return false;
                    }

                    // Retry-counter decrement moved to the synchronous receive
                    // path (see mark_received above).

                    if (item->second.fragments_achieved.size() == item->second.amount_fragments) {
                        active_reads_locked->erase(item);

                        auto dir_row       = res->second.dir_row;
                        bool is_downloaded = node->dfs()->is_file_already_downloaded(file_link.owner_id,
                                                                                     file_link.file_id,
                                                                                     dir_row.hash);
                        if (!is_downloaded) {
                            eLog("[Fragment] Ooops, something wrong. File not downloaded. File link: {}",
                                 file_link);
                            timer_runner(file_link);
                            return true;
                        }

                        bool notify_neighbours = res->second.notify_neighbours;

                        active_downloads_locked->erase(res);
                        // eLog("[Dfs] LoadManager::file_fragment_achieved, file downloaded: {}", file_link);

                        finish_him(file_link.owner_id, dir_row);

                        if (notify_neighbours)
                            broadcast_file_exist(file_link.owner_id, file_link.file_id);
                    } else {
                        res->second.last_fragment_received = std::chrono::system_clock::now();
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
                    return true;
                };

                if (!process_func(m_active_downloads_priority)) {
                    process_func(m_active_downloads);
                }
            }
        }
    });
}

void LoadManager::finish_him(const ActorId& owner_id, const Dfs::DirRow& dir_row) {
    {
        auto completed_locked = *m_completed_once;
        completed_locked->insert(Dfs::FileLink { .owner_id = owner_id, .file_id = dir_row.file_id });
    }
    eLog("[Load] DONE rank={} {} {}/{} size={}",
         node->dfs()->download_rank(owner_id, dir_row),
         dir_row.folder.value_or("-"),
         owner_id,
         dir_row.file_id,
         dir_row.size);

    Dfs::Tables::DirsFile::ActorSpace::update_file_state(node->dfs()->get_db_instance(),
                                                         owner_id,
                                                         dir_row.file_id,
                                                         Dfs::FileState::Ready);
    emit node->dfs()->added(owner_id, dir_row);
    emit node->dfs()->downloaded(owner_id, dir_row);

    // A finished transfer frees window slots; hand them to the next queued
    // download now instead of on the next periodic tick. Coalesced, so a burst
    // of small completed vectors costs one full pass.
    kick();
}

bool LoadManager::is_downloading(const Dfs::FileLink& file_link) const {
    constexpr auto ACTIVITY_TIMEOUT = std::chrono::seconds(60);
    auto now = std::chrono::system_clock::now();

    auto check_active = [&](const SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>>& downloads) -> bool {
        auto locked = *downloads;
        auto it = locked->find(file_link);
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
