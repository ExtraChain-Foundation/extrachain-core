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

#include <array>
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
    struct Attempts {
        int                                   counter { 0 };
        std::chrono::system_clock::time_point last_attempt {};
    };

    Dfs::DirRow dir_row;

    size_t                                amount_fragments;
    std::set<size_t>                      fragments_left;
    std::chrono::system_clock::time_point last_fragment_received {};
    std::chrono::system_clock::time_point queued {};

    bool notify_neighbours;
    bool forced { false };
    // Count of full source-exhaustion restarts: after 3 cycles the download goes
    // into an exponential cooldown instead of being dropped — the only reachable
    // peer may simply not have the content yet (leaf asks hub before hub fetched it).
    int source_refresh_cycles { 0 };
    int cooldown_rounds { 0 };
    std::chrono::system_clock::time_point cooldown_until {};

    std::set<std::string>                         identifier_storage_checker {};
    std::vector<std::pair<std::string, Attempts>> identifier_list {};
    // std::chrono::system_clock::time_point last_segment_time {}; // Time of last received segment
    // Dfs::FileState                        state { Dfs::FileState::Known };
    // std::unordered_set<std::string> tried_neighbors;

    // [[nodiscard]] std::chrono::milliseconds next_delay() const {
    //     return std::chrono::minutes(1) * (1 << attempt_count);
    // }

    // [[nodiscard]] bool can_retry() const {
    //     return std::chrono::system_clock::now() >= last_attempt + next_delay();
    // }

    // [[nodiscard]] bool is_stalled() const {
    //     // If last segment was received more than 30 seconds ago
    //     return std::chrono::system_clock::now() - last_segment_time > std::chrono::seconds(30);
    // }
};

enum class PullMode {
    All,
    Selective
};

class LoadManager : public QObject {
    Q_OBJECT
public:
    explicit LoadManager(ExtraChainNode* node, QObject* parent = nullptr);

    bool add_node_identifier(const Dfs::FileLink& file_link, std::string identifier);
    void remove_active_download(const Dfs::FileLinkFragment& file_link_fragment);
    void add_to_queue(const ActorId&     owner_id,
                      const Dfs::DirRow& dir_row,
                      const std::string& identifier,
                      const bool         notify_neighbours = false);
    void add_to_queue(const ActorId&                  owner_id,
                      const std::vector<Dfs::DirRow>& dir_rows,
                      const std::string&              identifier);

    // void process_next();
    void check_stalled_downloads(); // Check "stalled" downloads

    void share_stored_file(const Dfs::FileLinkFragment& file_link_fragment, const Responder& responder);
    void broadcast_file_exist(const ActorId& owner_id, const std::string& file_id);

    void file_fragment_achieved(const Dfs::Packets::FragmentData& file_content, const std::string& identifier);

    void finish_him(const ActorId& owner_id, const Dfs::DirRow& dir_row);

    bool is_downloading(const Dfs::FileLink& file_link) const;

    size_t active_downloads_size() {
        return m_active_downloads->size() + m_active_downloads_priority->size();
    }

private:
    void timer_runner(const Dfs::FileLink file_link_to_proceed = {});
    // "Vectors before files" gate state; a full scan of both pools, so callers cache it.
    bool compute_vectors_waiting();
    // Instant (coalesced) scheduler wakeup — without it new queue items waited for the next
    // 5-second timer tick.
    void kick();

    std::atomic_bool kick_pending_ { false };
    std::atomic_int  vector_gate_state_ { -1 };

    void        schedule_watchdog();
    std::size_t max_concurrent_downloads() const;
    std::size_t max_forced_downloads() const;

    ExtraChainNode* node;

    static constexpr int  MAX_ATTEMPTS  = 10;
    static constexpr auto STALL_TIMEOUT = std::chrono::seconds(30);

    PullMode pull_mode = PullMode::All;

    SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>> m_active_downloads;
    SafePtr<std::unordered_map<Dfs::FileLink, LoadInfo>> m_active_downloads_priority;
    SafePtr<std::map<Dfs::FileLinkFragment, std::chrono::system_clock::time_point>>
        m_amount_file_fragments_requests;

    struct ReadStorage {
        // uint64_t current_size;
        std::size_t      amount_fragments;
        std::set<size_t> fragments_achieved;
        // std::map<uint64_t, bool> offsets_read_progress;
    };

    SafePtr<std::unordered_map<Dfs::FileLink, ReadStorage>> m_active_reads;
    static constexpr std::size_t                            WRITE_STRIPES = 64;
    std::array<std::mutex, WRITE_STRIPES>                   m_write_file_mutexes;

    // Files that already finished downloading this session: their re-downloads (vector
    // hash-mismatch cycle) don't hold the "vectors before files" gate.
    SafePtr<std::set<Dfs::FileLink>> m_completed_once;

    QTimer* m_timer;
};
