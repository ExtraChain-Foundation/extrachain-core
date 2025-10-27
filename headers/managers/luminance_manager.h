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

#include "chain/actor_id.h"

#include <boost/describe/class.hpp>

class ExtraChainNode;
class DbConnector;
class Responder;

struct LuminanceData {
    std::unordered_map<NodeId, int> data;
};
BOOST_DESCRIBE_STRUCT(LuminanceData, (), (data))

namespace Luminance {
    constexpr int   TRUST_THRESHOLD    = 5;     // Абсолютная погрешность в единицах luminance
    constexpr float MATCH_THRESHOLD    = 0.90f; // 90% совпадение = хорошо
    constexpr float RELATIVE_THRESHOLD = 0.10f; // 10% относительная погрешность
} // namespace Luminance

class LuminanceManager {
public:
    LuminanceManager(ExtraChainNode *node);
    ~LuminanceManager() = default;

    bool init_db();
    void reset_db();

    LuminanceData read_all_luminances();
    LuminanceData read_luminances(const std::vector<NodeId> &node_ids);
    LuminanceData read_luminances(const std::vector<ActorId> &actors_ids);
    int           read_luminance(const ActorId &node_id);
    int           read_luminance(const NodeId &node_id);

    void increment(const NodeId &node_id);
    void decrement(const NodeId &node_id);
    void write_luminance(const NodeId &node_id, int luminance);
    void remove_old();

    void request_luminances(const std::vector<NodeId> &node_ids);
    void network_request_luminances(const std::vector<NodeId> &actors_ids, const Responder &responder);
    void network_response_luminances(const LuminanceData &luminance_data);

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

    ExtraChainNode *node;
};
