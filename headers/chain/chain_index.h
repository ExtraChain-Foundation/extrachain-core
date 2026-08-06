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

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "extrachain_global.h"
#include "utils/bignumber.h"

class ExtraChainNode;
struct Section;
class Transaction;

/**
 * @brief Compact representation of a transaction stored in the index.
 *
 * Amount stays as string to match the wire/storage canonical form (decimal).
 */
struct ChainIndexEntry {
    SectionId     section_id;
    std::string   sender;
    std::string   receiver;
    std::string   token;
    int           type;      // TransactionType enum value
    std::uint64_t timestamp; // ms
    std::string   amount;
};

/**
 * @brief Persistent transaction index on top of dag storage.
 *
 * Writes: one batched SQLite transaction per section via on_section_written().
 * Reads: prepared statements with compound indexes — sender/receiver/token/timestamp.
 *
 * Mode:
 *   DagMode::Full  — indexes every transaction (can answer any query).
 *   DagMode::Light — indexes only transactions involving a local wallet actor.
 *
 * The database (dag/cache/ChainIndex.db) is rebuildable from packs+hot; if the
 * file is missing or truncated, rebuild_from_disk() walks the on-disk chain and
 * repopulates from scratch with sync-off + journal-in-memory for speed.
 */
class EXTRACHAIN_EXPORT ChainIndex {
public:
    explicit ChainIndex(ExtraChainNode *node);
    ~ChainIndex();

    ChainIndex(const ChainIndex &)            = delete;
    ChainIndex &operator=(const ChainIndex &) = delete;

    // Called by Dag::write_section. Replaces rows for the section and batches all txs
    // in one SQLite transaction, so replays do not duplicate query results.
    void on_section_written(const Section &s);

    // All tx where sender == actor OR receiver == actor, optionally filtered by
    // token, newer-than timestamp filter, newest first. limit caps result size.
    std::vector<ChainIndexEntry> find_for_actor(const std::string &actor,
                                                const std::string &token            = {},
                                                std::uint64_t      before_timestamp = 0, // 0 == no upper bound
                                                int                limit            = 50) const;

    // Same but restricted to sender side only. Useful for "outgoing tx" views.
    std::vector<ChainIndexEntry> find_sent_by(const std::string &actor,
                                              const std::string &token            = {},
                                              std::uint64_t      before_timestamp = 0,
                                              int                limit            = 50) const;

    std::vector<ChainIndexEntry> find_received_by(const std::string &actor,
                                                  const std::string &token            = {},
                                                  std::uint64_t      before_timestamp = 0,
                                                  int                limit            = 50) const;

    // Count transactions of one type that involve the actor at or after the
    // supplied timestamp. This supports summary endpoints without loading DAG
    // sections or materializing all matching rows.
    std::uint64_t count_for_actor_by_type_since(const std::string &actor,
                                                int                type,
                                                std::uint64_t      since_timestamp) const;

    // Rebuild index from pack registry + hot sections. Blocking; safe to call
    // on startup if we suspect the index is stale (missing file, row-count
    // mismatch, etc.). Uses bulk-load pragmas (sync=off, journal=memory) and
    // restores safe settings afterwards.
    void rebuild_from_disk();

    // Drop all rows. Used by wipe flows.
    void clear();

    // Number of rows (heavy — uses COUNT(*); for rough estimates, prefer
    // last_indexed_section()).
    std::uint64_t row_count() const;

    // Highest section_id that has been indexed so far. Cheap.
    SectionId last_indexed_section() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
