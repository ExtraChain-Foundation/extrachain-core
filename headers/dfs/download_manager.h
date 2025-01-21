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

#pragma once

#include <queue>
#include <string>
#include <chrono>
#include <expected>
#include <unordered_set>

#include "dfs_utils.h"
#include "blockchain/actor_id.h"

class ExtraChainNode;

enum class DownloadError {
    NoNeighbors,
    NetworkError,
    FileNotFound
};

struct LoadInfo {
    ActorId                               actor_id;
    std::string                           file_id;
    int                                   attempt_count { 0 };
    std::chrono::system_clock::time_point last_attempt {};
    std::chrono::system_clock::time_point last_segment_time {}; // Время последнего полученного сегмента
    Dfs::FileState                        state { Dfs::FileState::Known };
    std::unordered_set<std::string>       tried_neighbors;

    [[nodiscard]] std::chrono::milliseconds next_delay() const {
        return std::chrono::minutes(1) * (1 << attempt_count);
    }

    [[nodiscard]] bool can_retry() const {
        return std::chrono::system_clock::now() >= last_attempt + next_delay();
    }

    [[nodiscard]] bool is_stalled() const {
        // Если последний сегмент был получен более 30 секунд назад
        return std::chrono::system_clock::now() - last_segment_time > std::chrono::seconds(30);
    }
};

class LoadManager {
public:
    LoadManager(ExtraChainNode* node);

    void add_to_queue(ActorId actor_id, std::string file_id, Dfs::FileState state);
    void process_next();
    void check_stalled_downloads(); // Проверка "зависших" загрузок

    // Тебе нужно будет добавить:
    // void on_search_result(const DfsSyncSearchResult& result);
    // void on_segment_received(const std::string& file_id); // Обновление времени последнего сегмента
    // void on_download_error(/* параметры ошибки */);

private:
    ExtraChainNode* node;

    static constexpr int  MAX_ATTEMPTS             = 10;
    static constexpr int  MAX_CONCURRENT_DOWNLOADS = 2;
    static constexpr auto STALL_TIMEOUT            = std::chrono::seconds(30);

    // current_upload
    std::queue<LoadInfo>                      download_queue;
    std::unordered_map<std::string, LoadInfo> active_downloads; // file_id -> LoadInfo

    [[nodiscard]] std::optional<LoadInfo> get_next_download();
    void move_to_queue_end(const std::string& file_id); // Перемещение "зависшей" загрузки в конец очереди

    // Тебе нужно будет добавить:
    // void send_search_request(const DownloadInfo& info);
    // std::string select_random_neighbor(const std::unordered_set<std::string>& excluded);
};
