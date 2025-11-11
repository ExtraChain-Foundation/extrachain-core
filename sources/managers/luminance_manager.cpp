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

#include "managers/luminance_manager.h"

#include <QDir>

#include "chain/actor_id.h"
#include "managers/extrachain_node.h"
#include "network/network_manager.h"
#include "utils/db_connector.h"
#include "utils/exc_logs.h"

#include <algorithm>
#include <cmath> 

LuminanceManager::LuminanceManager(ExtraChainNode *node)
    : node(node) {
    this->init_db();
}

bool LuminanceManager::init_db() {
    if (db_initialized_) {
        return true;
    }

    if (luminance_db_ && luminance_db_->is_open()) {
        db_initialized_ = true;
        return true;
    }

    // bool is_exists = QFile(QString::fromStdString(ChainConst::BALANCE_CACHE)).exists();

    QDir().mkdir(QString::fromStdString(Luminance::FOLDER));

    std::string db_path = Luminance::DATABASE;
    luminance_db_       = std::make_unique<DbConnector>(db_path);

    if (!luminance_db_->open()) {
        eCritical("[LuminanceManager] Failed to open database");
        return false;
    }

    // Create table if it doesn't exist
    luminance_db_->drop_table(Config::DataStorage::LUMINANCE_TABLE);
    bool success = luminance_db_->query(Config::DataStorage::LUMINANCE_TABLE_CREATE);

    if (!success) {
        eCritical("[LuminanceManager] Failed to create table");
        return false;
    }

    eLog("[LuminanceManager] Database initialized");
    db_initialized_ = true;
    return true;
}

void LuminanceManager::reset_db() {
    db_initialized_ = false;

    if (db_initialized_) {
        luminance_db_->close();
        QFile::remove(QString::fromStdString(Luminance::DATABASE));
    }

    luminance_db_.reset();
}

LuminanceData LuminanceManager::read_all_luminances() {
    LuminanceData result;

    auto rows = luminance_db_->select(fmt::format("SELECT * FROM {}", Config::DataStorage::LUMINANCE_TABLE));

    for (const auto &row : rows) {
        try {
            NodeId node_id;
            node_id.actor_id        = row.at("actor_id");
            node_id.node_identifier = row.at("node_identifier");

            int luminance        = std::stoi(row.at("luminance"));
            result.data[node_id] = luminance;
        } catch (const std::exception &) {
            continue;
        }
    }

    return result;
}

LuminanceData LuminanceManager::read_luminances(const std::vector<NodeId> &node_ids) {
    LuminanceData result;
    eTemp("[LuminanceManager] **TEST** read_luminances");
    if (node_ids.empty()) {
        return result;
    }

    std::string condition;
    for (size_t i = 0; i < node_ids.size(); ++i) {
        if (i > 0) {
            condition += " OR ";
        }
        condition += fmt::format("(actor_id = '{}' AND node_identifier = '{}')",
                                 node_ids[i].actor_id,
                                 node_ids[i].node_identifier);
    }
    eTemp("[LuminanceManager] **TEST** condition: {}", condition);

    std::string format = fmt::format("SELECT * FROM {} WHERE {}", Config::DataStorage::LUMINANCE_TABLE, condition);
    eTemp("[LuminanceManager] **TEST** format: {}", format);
    auto rows = luminance_db_->select(format);

    eTemp("[LuminanceManager] **TEST** luminance_db_->select rows count: {}", rows.size());
    for (const auto &row : rows) {
        try {
            NodeId node_id;
            node_id.actor_id        = row.at("actor_id");
            node_id.node_identifier = row.at("node_identifier");

            int luminance        = std::stoi(row.at("luminance"));
            result.data[node_id] = luminance;
        } catch (const std::exception &) {
            continue;
        }
    }

    return result;
}

LuminanceData LuminanceManager::read_luminances(const std::vector<ActorId> &actors_ids) {
    LuminanceData result;

    if (actors_ids.empty()) {
        return result;
    }

    std::string condition;
    for (size_t i = 0; i < actors_ids.size(); ++i) {
        if (i > 0) {
            condition += " OR ";
        }
        condition += fmt::format("actor_id = '{}'", actors_ids[i]);
    }

    auto rows = luminance_db_->select(
        fmt::format("SELECT * FROM {} WHERE {}", Config::DataStorage::LUMINANCE_TABLE, condition));

    for (const auto &row : rows) {
        try {
            NodeId node_id;
            node_id.actor_id        = row.at("actor_id");
            node_id.node_identifier = row.at("node_identifier");

            int luminance        = std::stoi(row.at("luminance"));
            result.data[node_id] = luminance;
        } catch (const std::exception &) {
            continue;
        }
    }

    return result;
}

int LuminanceManager::read_luminance(const ActorId &actor_id) {
    int  luminance = -1;
    auto rows      = luminance_db_->select(fmt::format("SELECT * FROM {} WHERE actor_id = '{}' LIMIT 1",
                                                  Config::DataStorage::LUMINANCE_TABLE,
                                                  actor_id));

    if (rows.empty()) {
        return luminance;
    }

    try {
        luminance = std::stoi(rows[0]["luminance"]);
    } catch (const std::exception &) {
        return luminance;
    }

    return luminance;
}

int LuminanceManager::read_luminance(const NodeId &node_id) {
    // TODO: memory cache result
    eTemp("[LuminanceManager] **TEST** read_luminance start");
    int luminance = -1;

    auto format_select = fmt::format("SELECT * FROM {} WHERE actor_id = '{}' AND node_identifier = '{}'",
                                          Config::DataStorage::LUMINANCE_TABLE,
                                          node_id.actor_id,
                                          node_id.node_identifier);
    eTemp("[LuminanceManager] **TEST** format_select: {}", format_select);
    auto rows = luminance_db_->select(format_select);
    eTemp("[LuminanceManager] **TEST** rows size: {}", rows.size());

    if (rows.empty() || rows.size() > 1) {
        return luminance;
    }

    try {
        luminance = std::stoi(rows[0]["luminance"]);
        eTemp("[LuminanceManager] **TEST** luminance: {}", luminance);
    } catch (const std::exception &) {
        return luminance;
    }

    eTemp("[LuminanceManager] **TEST** return luminance: {}", luminance);
    return luminance;
}

void LuminanceManager::increment(const NodeId &node_id) {
    this->update_luminance(node_id, Operation::Increment, 1);
}

void LuminanceManager::decrement(const NodeId &node_id) {
    this->update_luminance(node_id, Operation::Decrement);
}

void LuminanceManager::write_luminance(const NodeId &node_id, int luminance) {
    this->update_luminance(node_id, Operation::Set, luminance);
}

void LuminanceManager::remove_old() {
    // eTemp("[LuminanceManager] Remove old");
    auto now       = Utils::current_date_ms();
    auto threshold = now - Luminance::AUTOREMOVE_MS;
    luminance_db_->query(
        fmt::format("DELETE FROM {} WHERE timestamp < {}", Config::DataStorage::LUMINANCE_TABLE, threshold));
}

void LuminanceManager::request_luminances(const std::vector<NodeId> &node_ids) {
    eLog("[LuminanceManager] Starting new validation request for {} nodes", node_ids.size());

    active_validation_.emplace();
    active_validation_->timeout = std::chrono::steady_clock::now() + Luminance::VALIDATION_TIMEOUT;
    active_validation_->target_nodes.insert(node_ids.begin(), node_ids.end());

    node->network()->send_message(node_ids, MessageType::Luminance, SendMode::Neighbours, MessageStatus::Request);
}

void LuminanceManager::network_request_luminances(const std::vector<NodeId> &actors_ids,
                                                  const Responder           &responder) {
    LuminanceData response_data = this->read_luminances(actors_ids);
    response_data.source = responder.node_id();

    for (const auto& [node_id, luminance] : response_data.data) {
        eLog("[LUM]Node id: {} , ActorId: {} , Luminance: {}",
            node_id.node_identifier,
            node_id.actor_id.value(),
            luminance);
    }

    responder.send_response(response_data, MessageType::Luminance, SendMode::Focused, MessageStatus::Response);

    eTemp("[LuminanceManager] Sent luminance data for {} nodes", response_data.data.size());

}

void LuminanceManager::network_response_luminances(const LuminanceData &luminance_data) {
    eTemp("[LuminanceManager] **TEST** Received response");

    // 1. Проверяем, есть ли активный запрос на валидацию
    if (!active_validation_) {
        eLog("[LuminanceManager] Received response, but no validation active. Ignoring.");
        return;
    }

    // 2. Проверяем, кто прислал
    if (!luminance_data.source.has_value()) {
        eTemp("[LuminanceManager] Received response without a 'source' NodeId. Ignoring.");
        return;
    }
    NodeId responder_id = luminance_data.source.value();

    // 3. Проверяем таймаут
    auto now = std::chrono::steady_clock::now();
    if (now > active_validation_->timeout) {
        eLog("[LuminanceManager] Received response from {}, but validation window closed. Ignoring.", responder_id.node_identifier);

        // Если окно только что закрылось, запускаем финальную валидацию
        // и сбрасываем состояние, чтобы не запустить ее дважды.
        if (active_validation_->responses.size() >= Luminance::MIN_RESPONSES_FOR_CONSENSUS) {
             validate_consensus();
        }
        active_validation_.reset();

        return;
    }

    // 4. Сохраняем ответ
    eTemp("[LuminanceManager] Received valid response from: {}", responder_id.node_identifier);
    active_validation_->responses[responder_id] = luminance_data;

    // 5. Проверяем, не пора ли валидировать (по кол-ву)
    if (active_validation_->responses.size() >= Luminance::MIN_RESPONSES_FOR_CONSENSUS) {
        eLog("[LuminanceManager] Reached minimum responses ({}), running consensus...", Luminance::MIN_RESPONSES_FOR_CONSENSUS);
        validate_consensus();
        active_validation_.reset(); // Валидация завершена, сбрасываем
    }
}

void LuminanceManager::update_luminance(const NodeId &node_id, Operation op, int value) {
    eTemp("[LuminanceManager] update_luminance: {}, {}, {}", node_id, op, value);
    auto        now = Utils::current_date_ms();
    std::string update_expr;
    int         initial_value;

    switch (op) {
    case Operation::Increment:
        update_expr   = "luminance + 1";
        initial_value = 1;
        break;
    case Operation::Decrement:
        update_expr   = "MAX(0, luminance - 1)";
        initial_value = 0;
        break;
    case Operation::Set:
        update_expr   = std::to_string(std::max(0, value));
        initial_value = std::max(0, value);
        break;
    }

    luminance_db_->query(
        fmt::format("INSERT INTO {} (actor_id, node_identifier, luminance, timestamp) "
                    "VALUES ('{}', '{}', {}, {}) "
                    "ON CONFLICT(actor_id, node_identifier) DO UPDATE SET "
                    "luminance = {}, "
                    "timestamp = {}",
                    Config::DataStorage::LUMINANCE_TABLE,
                    node_id.actor_id,
                    node_id.node_identifier,
                    initial_value,
                    now,
                    update_expr,
                    now));
}

// === РЕАЛИЗАЦИЯ НОВЫХ ПРИВАТНЫХ МЕТОДОВ ===

/**
 * @brief Нормализует счетчики в доли [0, 1]
 */
std::unordered_map<NodeId, double> LuminanceManager::normalize(const LuminanceData &d) const {
    std::unordered_map<NodeId, double> out;
    long long total = 0;
    for (const auto &p : d.data) {
        total += static_cast<long long>(p.second);
    }

    if (total <= 0) return out;

    for (const auto &p : d.data) {
        out[p.first] = static_cast<double>(p.second) / static_cast<double>(total);
    }
    return out;
}

/**
 * @brief Вычисляет медианное (консенсусное) распределение
 */
std::unordered_map<NodeId, double> LuminanceManager::compute_consensus(
    const std::vector<std::unordered_map<NodeId, double>>& norms) const 
{
    std::unordered_map<NodeId, std::vector<double>> all_ratios;
    for (const auto &dist : norms) {
        for (const auto &p : dist) {
            all_ratios[p.first].push_back(p.second);
        }
    }

    std::unordered_map<NodeId, double> consensus;
    for (auto &kv : all_ratios) {
        auto &vec = kv.second;
        if (vec.empty()) continue;
        std::sort(vec.begin(), vec.end());
        size_t n = vec.size();
        double median = (n % 2 == 1) ? vec[n/2] : (vec[n/2 - 1] + vec[n/2]) / 2.0;
        consensus[kv.first] = median;
    }
    return consensus;
}

/**
 * @brief Проверяет узел на "вменяемость" (минимальная статистика, нет экстремальных скачков)
 */
bool LuminanceManager::sanity_check(const ValidationState& state, const NodeId& node) const {
    int min_seen = INT_MAX;
    int max_seen = -1;
    bool any = false;

    for (const auto &p : state.responses) { // p.first = responder, p.second = LuminanceData
        auto it = p.second.data.find(node);
        if (it == p.second.data.end()) continue;

        any = true;
        min_seen = std::min(min_seen, it->second);
        max_seen = std::max(max_seen, it->second);
    }

    if (!any) return false; 

    if (min_seen > 0 && (static_cast<double>(max_seen) / static_cast<double>(min_seen) > Luminance::EXTREME_RATIO_LIMIT)) {
        eTemp("[Luminance] Sanity check FAILED for {}: extreme ratio (min={}, max={})", node.node_identifier, min_seen, max_seen);
        return false;
    }

    if (max_seen < Luminance::MIN_EVENTS_FOR_STATS) {
        eTemp("[Luminance] Sanity check FAILED for {}: not enough events (max_seen={})", node.node_identifier, max_seen);
        return false;
    }

    return true;
}

/**
 * @brief Классифицирует узлы на основе консенсуса
 */
std::unordered_map<NodeId, LuminanceManager::NodeClassification> LuminanceManager::classify_nodes(
        const ValidationState& state, 
        const std::unordered_map<NodeId, double>& consensus) const
{
    std::unordered_map<NodeId, NodeClassification> result;
    const auto& responses = state.responses;

    std::vector<std::unordered_map<NodeId, double>> norms;
    norms.reserve(responses.size());
    for (const auto &p : responses) {
        norms.push_back(normalize(p.second));
    }

    for (const auto &kv : consensus) {
        const NodeId &node = kv.first;
        double cons_ratio = kv.second;

        bool sane = sanity_check(state, node);

        int matches = 0;
        int total_with_node = 0;

        for (const auto &dist : norms) {
            auto it = dist.find(node);
            if (it == dist.end()) continue;

            total_with_node++;
            double observed = it->second;
            double dev = std::abs(observed - cons_ratio) / std::max(cons_ratio, Luminance::EPSILON);

            if (dev <= Luminance::RATIO_TOLERANCE) {
                matches++;
            }
        }

        double match_rate = (total_with_node > 0) ? static_cast<double>(matches) / total_with_node : 0.0;

        NodeClassification cls;
        cls.consensus_ratio = cons_ratio;
        cls.match_rate = match_rate;

        if (!sane) {
            cls.state = NodeClassification::State::Unknown;
        } else {
            if (match_rate >= Luminance::MATCH_THRESHOLD) {
                cls.state = NodeClassification::State::Honest;
            }
            else if (match_rate <= (1.0 - Luminance::MATCH_THRESHOLD)) {
                cls.state = NodeClassification::State::Suspicious;
            }
            else {
                cls.state = NodeClassification::State::Unknown;
            }
        }
        result[node] = cls;
    }
    return result;
}

void LuminanceManager::validate_consensus() {
    if (!active_validation_) {
        eTemp("[LuminanceManager] validate_consensus called, but no active validation.");
        return;
    }

    const auto& state = active_validation_.value();

    if (state.responses.size() < Luminance::MIN_RESPONSES_FOR_CONSENSUS) {
         eTemp("[LuminanceManager] Not enough responses ({}) to run consensus.", state.responses.size());
         return;
    }

    eTemp("[LuminanceManager] === Running Consensus Analysis ===");
    eTemp("[LuminanceManager] Total responses: {}", state.responses.size());

    // 1. Нормализуем все ответы
    std::vector<std::unordered_map<NodeId, double>> norms;
    norms.reserve(state.responses.size());
    for (const auto &p : state.responses) // p.first=NodeId, p.second=LuminanceData
    { 
        norms.push_back(normalize(p.second));
    }

    // 2. Считаем медиану (консенсус)
    auto consensus = compute_consensus(norms);

    // 3. Классифицируем узлы
    auto classifications = classify_nodes(state, consensus);

    // 4. Логируем результат
    int honest = 0, suspicious = 0, unknown = 0;
    for (const auto &p : classifications) {
        const NodeId &nid = p.first;
        const auto &c = p.second;

        const char* state_str;
        switch(c.state) {
            case NodeClassification::State::Honest:     state_str = "HONEST"; honest++; break;
            case NodeClassification::State::Suspicious: state_str = "SUSPICIOUS"; suspicious++; break;
            case NodeClassification::State::Unknown:    state_str = "UNKNOWN"; unknown++; break;
        }

        eLog("[LUMINANCE RESULT] Node={:<15} | State: {:<10} | Match: {:.0f}% | Consensus Ratio: {:.4f}",
             nid.node_identifier,
             state_str,
             c.match_rate * 100.0,
             c.consensus_ratio);
    }
    eLog("[LuminanceManager] === Consensus Finished: Honest={}, Suspicious={}, Unknown={} ===", honest, suspicious, unknown);
}
