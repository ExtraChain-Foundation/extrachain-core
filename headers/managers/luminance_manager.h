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

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include <boost/describe/class.hpp>

namespace ExtraChain::Core {
    class ExtraChainNode;
}
class DbConnector;
struct NodeId;

class LuminanceManager {
public:
    LuminanceManager(ExtraChain::Core::ExtraChainNode *node);
    ~LuminanceManager() = default;

    bool init_db();
    void reset_db();

    int read_luminance(const NodeId &node_id);

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

private:
    std::unique_ptr<DbConnector> luminance_db_;
    bool                         db_initialized_ = false; // Whether db is initialized

    // In-memory mirror of the table (the "TODO: memory cache result" in read_luminance).
    // Every inbound network message calls read_luminance, and broadcasts also call
    // increment — two sqlite round trips per message, each taking the global db mutex
    // that serialises every database in the process. Under a normal transaction flow
    // that starved ordered node work outright: on a six-node stand the 10-second status
    // timer stopped firing altogether while the node kept accepting console input, so
    // no periodic work ran and nodes silently stopped syncing.
    //
    // The cache is authoritative once loaded: this table is only ever written through
    // this class, so nothing else can change it behind our back.
    mutable std::mutex                cache_mutex_;
    std::unordered_map<std::string, int> luminance_cache_;
    bool                                 cache_loaded_ = false;

    void load_cache();

    ExtraChain::Core::ExtraChainNode *node;
};
