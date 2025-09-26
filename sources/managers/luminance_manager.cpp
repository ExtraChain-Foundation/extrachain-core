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

int LuminanceManager::read_luminance(const NodeId &node_id) {
    // TODO: memory cache result
    // eTemp("[LuminanceManager] Read {}", node_id);

    auto node_id_str = fmt::format("{}_{}", node_id.actor_id, node_id.node_identifier);
    int  luminance   = -1;

    auto rows = luminance_db_->select(
        fmt::format("SELECT * FROM {} WHERE node_id = '{}'", Config::DataStorage::LUMINANCE_TABLE, node_id_str));

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
    eTemp("[LuminanceManager] Remove old");
    auto now       = Utils::current_date_ms();
    auto threshold = now - Luminance::AUTOREMOVE_MS;
    luminance_db_->query(fmt::format("DELETE FROM luminance WHERE timestamp < {}", threshold));
}

void LuminanceManager::update_luminance(const NodeId &node_id, Operation op, int value) {
    eTemp("[LuminanceManager] update_luminance: {}, {}, {}", node_id, op, value);
    auto        node_id_str = fmt::format("{}_{}", node_id.actor_id, node_id.node_identifier);
    auto        now         = Utils::current_date_ms();
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
        fmt::format("INSERT INTO luminance (node_id, luminance, timestamp) VALUES ('{}', {}, {}) "
                    "ON CONFLICT(node_id) DO UPDATE SET "
                    "luminance = {}, "
                    "timestamp = {}",
                    node_id_str,
                    initial_value,
                    now,
                    update_expr,
                    now));
}
