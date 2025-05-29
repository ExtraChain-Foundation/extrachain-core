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
#include <unordered_map>
#include "dfs/dfs_utils.h"
#include "chain/actor_id.h"
#include "network/network_manager.h"

class ExtraChainNode;

enum class DownloadError {
    NoNeighbors,
    NetworkError,
    FileNotFound
};

struct LoadInfo {
    Dfs::DirRow                           dir_row;
    int                                   attempt_count { 0 };
    std::chrono::system_clock::time_point last_attempt {};
    std::chrono::system_clock::time_point last_segment_time {}; // Time of last received segment
    // Dfs::FileState                        state { Dfs::FileState::Known };
    std::unordered_set<std::string> tried_neighbors;

    [[nodiscard]] std::chrono::milliseconds next_delay() const {
        return std::chrono::minutes(1) * (1 << attempt_count);
    }

    [[nodiscard]] bool can_retry() const {
        return std::chrono::system_clock::now() >= last_attempt + next_delay();
    }

    [[nodiscard]] bool is_stalled() const {
        // If last segment was received more than 30 seconds ago
        return std::chrono::system_clock::now() - last_segment_time > std::chrono::seconds(30);
    }
};

enum class PullMode {
    All,
    Selective
};

class LoadManager {
public:
    explicit LoadManager(ExtraChainNode* node);

    void add_to_queue(const ActorId& owner_id, const Dfs::DirRow& dir_row, std::string identifier);
    void add_to_queue(const ActorId& owner_id, const std::vector<Dfs::DirRow>& dir_rows, std::string identifier);

    void check_all_files(std::string identifier);

    // void process_next();
    void check_stalled_downloads(); // Check "stalled" downloads

    // You'll need to add:
    // void on_search_result(const DfsSyncSearchResult& result);
    // void on_segment_received(const std::string& file_id); // Update time of last segment
    // void on_download_error(/* error parameters */);

    void broadcast_stored_file(const ActorId&     owner_id,
                               const std::string& file_id,
                               const Responder&   responder = Responder());

    void network_fragment(const Dfs::Packets::FragmentData& fragment_data);

    void finish_him(const ActorId& owner_id, const Dfs::DirRow& dir_row);

private:
    ExtraChainNode* node;

    static constexpr int  MAX_ATTEMPTS             = 10;
    static constexpr int  MAX_CONCURRENT_DOWNLOADS = 2;
    static constexpr auto STALL_TIMEOUT            = std::chrono::seconds(30);

    PullMode pull_mode = PullMode::All;

    std::queue<LoadInfo> download_queue;

    QMutex mutex;

public:
    std::unordered_map<Dfs::FileLink, LoadInfo> active_downloads;

private:
    // [[nodiscard]] std::optional<LoadInfo> get_next_download();
    void move_to_queue_end(const Dfs::FileLink& file_link); // Move stalled download to end of queue

    // You'll need to add:
    // void send_search_request(const LoadInfo& info);
    // std::string select_random_neighbor(const std::unordered_set<std::string>& excluded);
};
