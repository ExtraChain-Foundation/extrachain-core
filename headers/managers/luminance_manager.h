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

#include <boost/describe/class.hpp>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

class ExtraChainNode;
class DbConnector;
struct NodeId;

class LuminanceManager {
public:
    LuminanceManager(ExtraChainNode *node);
    ~LuminanceManager() = default;

    bool init_db();
    void reset_db();

    int read_luminance(const NodeId &node_id);
    std::optional<int> cached_luminance(const NodeId &node_id) const;

    void increment(const NodeId &node_id);
    void decrement(const NodeId &node_id);
    void write_luminance(const NodeId &node_id, int luminance);
    void remove_old();

private:
    enum class Operation {
        Increment,
        Decrement,
        Set
    };

    void update_luminance(const NodeId &node_id, Operation op, int value = 0);
    void invalidate_cache(const std::string *key = nullptr);

private:
    std::unique_ptr<DbConnector> luminance_db_;
    bool                         db_initialized_ = false; // Whether db is initialized
    static constexpr std::size_t CacheEntries = 1024;
    static constexpr std::size_t CacheKeyBytes = 256;
    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, int> cache_;
    std::uint64_t cache_generation_ = 0;

    ExtraChainNode *node;
};
