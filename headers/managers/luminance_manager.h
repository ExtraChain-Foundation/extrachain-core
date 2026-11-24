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

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

class ExtraChainNode;
struct NodeId;

// Broadcast counts per sending node, kept only in memory: an entry lives 30 s after its last packet, so a disk
// copy never outlives a restart and only made every received packet wait for SQLite
class LuminanceManager {
public:
    using Clock = std::chrono::steady_clock;

    static constexpr std::size_t MaxEntries  = 65536;
    static constexpr std::size_t MaxKeyBytes = 256;

    explicit LuminanceManager(ExtraChainNode *node, std::function<Clock::time_point()> clock = {});
    ~LuminanceManager() = default;

    bool init_db();
    void reset_db();

    int read_luminance(const NodeId &node_id);
    std::optional<int> cached_luminance(const NodeId &node_id) const;

    void increment(const NodeId &node_id);
    void decrement(const NodeId &node_id);
    void write_luminance(const NodeId &node_id, int luminance);
    void remove_old();

    std::size_t size() const;

private:
    enum class Operation {
        Increment,
        Decrement,
        Set
    };

    struct Entry {
        int               luminance;
        Clock::time_point seen;
    };

    void update_luminance(const NodeId &node_id, Operation op, int value = 0);
    Clock::time_point now() const;

private:
    mutable std::mutex                     mutex_;
    std::unordered_map<std::string, Entry> entries_;
    std::function<Clock::time_point()>     clock_;
    bool                                   full_logged_ = false;

    ExtraChainNode *node;
};
