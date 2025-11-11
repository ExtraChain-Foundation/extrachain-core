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
#include <optional>
#include <chrono>
#include <set>

class ExtraChainNode;
class DbConnector;
class Responder;

struct LuminanceData {
    std::unordered_map<NodeId, int> data;
    // узел который отправил реквест
    std::optional<NodeId> source; 
};
BOOST_DESCRIBE_STRUCT(LuminanceData, (), (data, source))

namespace Luminance {
    constexpr int   TRUST_THRESHOLD    = 5;
    constexpr float MATCH_THRESHOLD    = 0.90f;
    constexpr float RELATIVE_THRESHOLD = 0.10f;

    constexpr size_t MIN_RESPONSES_FOR_CONSENSUS = 3;
    constexpr std::chrono::seconds VALIDATION_TIMEOUT = std::chrono::seconds(10);
    constexpr double RATIO_TOLERANCE = 0.15; // 15%
    constexpr int MIN_EVENTS_FOR_STATS = 10;
    constexpr double EXTREME_RATIO_LIMIT = 100.0;
    constexpr double EPSILON = 1e-6;
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

    struct ValidationState {
        std::unordered_map<NodeId, LuminanceData> responses; // <NodeId ответившего, Его данные>
        std::chrono::steady_clock::time_point timeout;
        std::set<NodeId> target_nodes; // О ком мы спрашивали
    };

    void validate_consensus();

    struct NodeClassification {
        enum class State { Honest, Suspicious, Unknown };
        State state = State::Unknown;
        double consensus_ratio = 0.0;
        double match_rate = 0.0;
    };

    std::unordered_map<NodeId, double> normalize(const LuminanceData &d) const;
    std::unordered_map<NodeId, double> compute_consensus(const std::vector<std::unordered_map<NodeId, double>>& norms) const;
    std::unordered_map<NodeId, NodeClassification> classify_nodes(const ValidationState& state, const std::unordered_map<NodeId, double>& consensus) const;
    bool sanity_check(const ValidationState& state, const NodeId& node) const;

private:
    std::unique_ptr<DbConnector>   luminance_db_;
    bool                           db_initialized_ = false; // Whether db is initialized
    std::optional<ValidationState> active_validation_;

    ExtraChainNode *node;
};
