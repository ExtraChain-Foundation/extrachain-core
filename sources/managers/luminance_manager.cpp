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
    // eTemp("[LuminanceManager] Read {}", node_id);

    int luminance = -1;

    auto rows =
        luminance_db_->select(fmt::format("SELECT * FROM {} WHERE actor_id = '{}' AND node_identifier = '{}'",
                                          Config::DataStorage::LUMINANCE_TABLE,
                                          node_id.actor_id,
                                          node_id.node_identifier));

    if (rows.empty() || rows.size() > 1) {
        return luminance;
    }

    try {
        luminance = std::stoi(rows[0]["luminance"]);
    } catch (const std::exception &) {
        return luminance;
    }

    return luminance;
}

void LuminanceManager::increment(const NodeId &node_id) {
    this->update_luminance(node_id, Operation::Increment);
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
    node->network()->send_message(node_ids, MessageType::Luminance, SendMode::Neighbours, MessageStatus::Request);
}

void LuminanceManager::network_request_luminances(const std::vector<NodeId> &actors_ids,
                                                  const Responder           &responder) {
    // Обработка запроса и отправка ответа

    // Читаем luminance для запрошенных NodeId одним запросом
    LuminanceData response_data = this->read_luminances(actors_ids);

    // Отправляем ответ через responder
    responder.send_response(response_data, MessageType::Luminance, SendMode::Focused, MessageStatus::Response);

    eTemp("[LuminanceManager] Sent luminance data for {} nodes", response_data.data.size());
}

void LuminanceManager::network_response_luminances(const LuminanceData &luminance_data) {
    if (luminance_data.data.empty()) {
        return;
    }

    // Извлекаем NodeId для запроса
    std::vector<NodeId> node_ids;
    for (const auto &[node_id, _] : luminance_data.data) {
        node_ids.push_back(node_id);
    }

    // Читаем наши локальные коэффициенты
    LuminanceData our_data = this->read_luminances(node_ids);

    // Статистика валидации
    int matching    = 0;
    int mismatching = 0;
    int missing     = 0;

    for (const auto &[node_id, external_luminance] : luminance_data.data) {
        auto it = our_data.data.find(node_id);

        if (it == our_data.data.end()) {
            missing++;
            continue;
        }

        int difference = std::abs(it->second - external_luminance);

        if (difference <= Luminance::TRUST_THRESHOLD) {
            matching++;
        } else {
            mismatching++;
        }
    }

    eTemp("[LuminanceManager] Validation: matching={}, mismatching={}, missing={}",
          matching,
          mismatching,
          missing);
}

void LuminanceManager::update_luminance(const NodeId &node_id, Operation op, int value) {
    // eTemp("[LuminanceManager] update_luminance: {}, {}, {}", node_id, op, value);
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
