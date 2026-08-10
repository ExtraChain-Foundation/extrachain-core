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

    {
        std::lock_guard lock(cache_mutex_);
        luminance_cache_.clear();
        cache_loaded_ = false;
    }

    if (db_initialized_) {
        luminance_db_->close();
        QFile::remove(QString::fromStdString(Luminance::DATABASE));
    }

    luminance_db_.reset();
}

void LuminanceManager::load_cache() {
    // Caller holds cache_mutex_.
    luminance_cache_.clear();

    auto rows = luminance_db_->select(fmt::format("SELECT * FROM {}", Config::DataStorage::LUMINANCE_TABLE));
    for (auto &row : rows) {
        try {
            luminance_cache_[row["node_id"]] = std::stoi(row["luminance"]);
        } catch (const std::exception &) {
            // A malformed row is worth ignoring, not worth failing the whole load over.
        }
    }

    cache_loaded_ = true;
}

int LuminanceManager::read_luminance(const NodeId &node_id) {
    // Served from memory: this runs on every inbound network message, and going to
    // sqlite here took the process-wide db mutex often enough to starve the Qt event
    // loop — periodic timers stopped firing and nodes stopped syncing. See the note on
    // luminance_cache_ in the header.
    auto node_id_str = fmt::format("{}_{}", node_id.actor_id, node_id.node_identifier);

    std::lock_guard lock(cache_mutex_);
    if (!cache_loaded_) {
        load_cache();
    }

    auto it = luminance_cache_.find(node_id_str);
    return it == luminance_cache_.end() ? -1 : it->second;
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
    luminance_db_->query(fmt::format("DELETE FROM luminance WHERE timestamp < {}", threshold));

    // Which rows the DELETE actually removed depends on timestamps the cache does not
    // track, so reload rather than guess. This runs rarely, unlike read/increment.
    std::lock_guard lock(cache_mutex_);
    if (cache_loaded_) {
        load_cache();
    }
}

void LuminanceManager::update_luminance(const NodeId &node_id, Operation op, int value) {
    // eTemp("[LuminanceManager] update_luminance: {}, {}, {}", node_id, op, value);
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

    // Mirror the same arithmetic into the cache rather than invalidating it: dropping
    // the cache here would send the next read straight back to sqlite, which is the
    // cost this cache exists to avoid — and writes happen on every broadcast.
    {
        std::lock_guard lock(cache_mutex_);
        if (cache_loaded_) {
            auto it = luminance_cache_.find(node_id_str);
            if (it == luminance_cache_.end()) {
                luminance_cache_[node_id_str] = initial_value;
            } else {
                switch (op) {
                case Operation::Increment:
                    it->second += 1;
                    break;
                case Operation::Decrement:
                    it->second = std::max(0, it->second - 1);
                    break;
                case Operation::Set:
                    it->second = std::max(0, value);
                    break;
                }
            }
        }
    }
}
