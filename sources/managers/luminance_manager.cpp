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
#include <QFile>
#include <algorithm>
#include <limits>

#include "chain/actor_id.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"

namespace {
std::string luminance_key(const NodeId &node_id) {
    return fmt::format("{}_{}", node_id.actor_id, node_id.node_identifier);
}
} // namespace

LuminanceManager::LuminanceManager(ExtraChainNode *node, std::function<Clock::time_point()> clock)
    : clock_(std::move(clock)), node(node) {
    this->init_db();
}

bool LuminanceManager::init_db() {
    // The SQLite store of older builds holds nothing that is still valid after a restart
    bool removed = false;
    for (const auto *suffix : { "", "-wal", "-shm", "-journal" }) {
        removed |= QFile::remove(QString::fromStdString(Luminance::DATABASE + suffix));
    }
    QDir().rmdir(QString::fromStdString(Luminance::FOLDER));
    if (removed) {
        eLog("[LuminanceManager] Removed the legacy database, luminance is kept in memory");
    }
    return true;
}

void LuminanceManager::reset_db() {
    std::lock_guard lock(mutex_);
    entries_.clear();
    full_logged_ = false;
}

int LuminanceManager::read_luminance(const NodeId &node_id) {
    const auto      key = luminance_key(node_id);
    std::lock_guard lock(mutex_);
    const auto      found = entries_.find(key);
    return found == entries_.end() ? -1 : found->second.luminance;
}

std::optional<int> LuminanceManager::cached_luminance(const NodeId &node_id) const {
    const auto       key = luminance_key(node_id);
    std::unique_lock lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) {
        return std::nullopt;
    }
    const auto found = entries_.find(key);
    return found == entries_.end() ? -1 : found->second.luminance;
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
    const auto      threshold = now() - std::chrono::milliseconds(Luminance::AUTOREMOVE_MS);
    std::lock_guard lock(mutex_);
    std::erase_if(entries_, [threshold](const auto &entry) { return entry.second.seen < threshold; });
    if (entries_.size() < MaxEntries) {
        full_logged_ = false;
    }
}

std::size_t LuminanceManager::size() const {
    std::lock_guard lock(mutex_);
    return entries_.size();
}

void LuminanceManager::update_luminance(const NodeId &node_id, Operation op, int value) {
    auto            key  = luminance_key(node_id);
    const auto      seen = now();
    std::lock_guard lock(mutex_);
    auto            found = entries_.find(key);
    if (found == entries_.end()) {
        // A sender that does not fit keeps the default luminance until old entries expire
        if (key.size() > MaxKeyBytes) {
            return;
        }
        if (entries_.size() >= MaxEntries) {
            if (!full_logged_) {
                full_logged_ = true;
                eWarning("[LuminanceManager] {} senders tracked, new ones wait for expiry", entries_.size());
            }
            return;
        }
        const int initial = op == Operation::Increment ? 1 : op == Operation::Set ? std::max(0, value) : 0;
        entries_.emplace(std::move(key), Entry { initial, seen });
        return;
    }
    auto &entry = found->second;
    switch (op) {
    case Operation::Increment:
        if (entry.luminance < std::numeric_limits<int>::max()) {
            ++entry.luminance;
        }
        break;
    case Operation::Decrement:
        entry.luminance = std::max(0, entry.luminance - 1);
        break;
    case Operation::Set:
        entry.luminance = std::max(0, value);
        break;
    }
    entry.seen = seen;
}

LuminanceManager::Clock::time_point LuminanceManager::now() const {
    return clock_ ? clock_() : Clock::now();
}
