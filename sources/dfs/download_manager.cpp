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

#include "dfs/download_manager.h"

#include "managers/extrachain_node.h"
#include "utils/exc_logs.h"

// Implementation file will contain network-related includes that you'll add later
// #include "network/dfs_sync_messages.h"

LoadManager::LoadManager(ExtraChainNode* node)
    : node(node) {
}

void LoadManager::add_to_queue(ActorId actor_id, std::string file_id, Dfs::FileState state) {
    eLog("Adding file to download queue: {} (state: {})", file_id, static_cast<int>(state));

    // Don't add if already in queue or active downloads
    if (active_downloads.contains(file_id)) {
        return;
    }

    // bool file_in_queue = std::any_of(download_queue.cbegin(), download_queue.cend(), [&file_id](const auto&
    // info) {
    //     return info.file_id == file_id;
    // });

    // if (file_in_queue) {
    //     return;
    // }

    // download_queue.push(DownloadInfo { .actor_id     = actor_id,
    //                                    .file_id      = file_id,
    //                                    .last_attempt = std::chrono::system_clock::now(),
    //                                    .state        = state });

    // // Try to process queue if we have space for new downloads
    // if (active_downloads.size() < MAX_CONCURRENT_DOWNLOADS) {
    //     process_next();
    // }
}

void LoadManager::process_next() {
    // Check if we can add more downloads
    while (active_downloads.size() < MAX_CONCURRENT_DOWNLOADS) {
        auto next = get_next_download();
        if (!next) {
            break; // No more items to process
        }

        // Здесь тебе нужно будет добавить:
        // send_search_request(*next);

        next->last_attempt              = std::chrono::system_clock::now();
        next->last_segment_time         = std::chrono::system_clock::now();
        active_downloads[next->file_id] = *next;
    }
}

std::optional<LoadInfo> LoadManager::get_next_download() {
    while (!download_queue.empty()) {
        auto info = download_queue.front();
        download_queue.pop();

        if (info.attempt_count >= MAX_ATTEMPTS) {
            eWarning("Max attempts reached for file: {}", info.file_id);
            continue;
        }

        if (!info.can_retry()) {
            // Put back in queue if it's too early to retry
            download_queue.push(info);
            return std::nullopt;
        }

        info.attempt_count++;
        return info;
    }
    return std::nullopt;
}

void LoadManager::check_stalled_downloads() {
    std::vector<std::string> stalled_files;

    // Find stalled downloads
    for (const auto& [file_id, info] : active_downloads) {
        if (info.is_stalled()) {
            stalled_files.push_back(file_id);
        }
    }

    // Move stalled downloads to queue end
    for (const auto& file_id : stalled_files) {
        move_to_queue_end(file_id);
    }
}

void LoadManager::move_to_queue_end(const std::string& file_id) {
    auto it = active_downloads.find(file_id);
    if (it == active_downloads.end()) {
        return;
    }

    eWarning("Moving stalled download to queue end: {}", file_id);

    auto info = it->second;
    active_downloads.erase(it);
    download_queue.push(info);

    // Try to start next download
    process_next();
}

/*
Тебе нужно будет добавить:

void DownloadManager::on_search_result(const DfsSyncSearchResult& result) {
 // Обработка результата поиска
 // Если файл найден - начать загрузку
 // Если нет - пробовать у другого соседа или пометить как ненайденный
}

void DownloadManager::on_segment_received(const std::string& file_id) {
 // Обновление времени последнего сегмента
 auto it = active_downloads.find(file_id);
 if (it != active_downloads.end()) {
     it->second.last_segment_time = std::chrono::system_clock::now();
 }
}
*/
