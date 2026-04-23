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

#include "chain/chain_index.h"

#include <sqlite3.h>

#include <filesystem>

#include "chain/dag.h"
#include "chain/transaction.h"
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"

namespace {

constexpr const char *DB_FILENAME = "ChainIndex.db";

// Schema:
//   tx_index(hash PK, section_id, sender, receiver, token, type, timestamp, amount)
// Indexes chosen for the two dominant query shapes:
//   "recent tx for actor (any token)"    -> idx_sender, idx_receiver
//   "recent tx for actor (given token)"  -> idx_sender_token, idx_receiver_token
// idx_token helps global token activity feeds.
constexpr const char *SCHEMA_SQL = R"(
    CREATE TABLE IF NOT EXISTS tx_index (
        hash       TEXT PRIMARY KEY,
        section_id INTEGER NOT NULL,
        sender     TEXT    NOT NULL,
        receiver   TEXT    NOT NULL,
        token      TEXT    NOT NULL,
        type       INTEGER NOT NULL,
        timestamp  INTEGER NOT NULL,
        amount     TEXT    NOT NULL
    ) WITHOUT ROWID;
    CREATE INDEX IF NOT EXISTS idx_sender          ON tx_index(sender, timestamp DESC);
    CREATE INDEX IF NOT EXISTS idx_receiver        ON tx_index(receiver, timestamp DESC);
    CREATE INDEX IF NOT EXISTS idx_token           ON tx_index(token, timestamp DESC);
    CREATE INDEX IF NOT EXISTS idx_sender_token    ON tx_index(sender, token, timestamp DESC);
    CREATE INDEX IF NOT EXISTS idx_receiver_token  ON tx_index(receiver, token, timestamp DESC);
    CREATE INDEX IF NOT EXISTS idx_section         ON tx_index(section_id);
)";

std::uint64_t section_to_u64(const SectionId &id) {
    auto i = id.to_int();
    if (i.has_value() && *i >= 0) return static_cast<std::uint64_t>(*i);
    try {
        return std::stoull(id.to_string());
    } catch (...) {
        return 0;
    }
}

} // namespace

struct ChainIndex::Impl {
    ExtraChainNode *node = nullptr;
    sqlite3        *db   = nullptr;

    // Prepared statements reused across writes/reads. Rebind + step + reset.
    sqlite3_stmt *stmt_insert                = nullptr;
    sqlite3_stmt *stmt_find_by_hash          = nullptr;
    sqlite3_stmt *stmt_find_for_actor        = nullptr;
    sqlite3_stmt *stmt_find_for_actor_token  = nullptr;
    sqlite3_stmt *stmt_find_sent             = nullptr;
    sqlite3_stmt *stmt_find_sent_token       = nullptr;
    sqlite3_stmt *stmt_find_recv             = nullptr;
    sqlite3_stmt *stmt_find_recv_token       = nullptr;
    sqlite3_stmt *stmt_row_count             = nullptr;
    sqlite3_stmt *stmt_last_section          = nullptr;

    mutable std::mutex write_mutex;

    ~Impl() {
        auto finalize = [](sqlite3_stmt *&s) {
            if (s) sqlite3_finalize(s);
            s = nullptr;
        };
        finalize(stmt_insert);
        finalize(stmt_find_by_hash);
        finalize(stmt_find_for_actor);
        finalize(stmt_find_for_actor_token);
        finalize(stmt_find_sent);
        finalize(stmt_find_sent_token);
        finalize(stmt_find_recv);
        finalize(stmt_find_recv_token);
        finalize(stmt_row_count);
        finalize(stmt_last_section);
        if (db) sqlite3_close(db);
    }

    bool exec(const char *sql) {
        char *err = nullptr;
        int   rc  = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::string msg = err ? err : "?";
            sqlite3_free(err);
            eWarning("[ChainIndex] SQL error: {} | while executing: {}", msg, sql);
            return false;
        }
        return true;
    }

    sqlite3_stmt *prepare(const char *sql) {
        sqlite3_stmt *s = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) {
            eWarning("[ChainIndex] prepare failed: {} | sql: {}", sqlite3_errmsg(db), sql);
            return nullptr;
        }
        return s;
    }

    bool open() {
        std::filesystem::create_directories(ChainConst::DAG_CACHE_FOLDER);
        auto path = ChainConst::DAG_CACHE_FOLDER + "/" + DB_FILENAME;

        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            eWarning("[ChainIndex] Failed to open {}: {}", path, sqlite3_errmsg(db));
            sqlite3_close(db);
            db = nullptr;
            return false;
        }

        // Settings that stay on for normal operation. rebuild_from_disk()
        // temporarily flips these to unsafe-but-fast and restores at the end.
        exec("PRAGMA journal_mode = WAL");
        exec("PRAGMA synchronous = NORMAL");
        exec("PRAGMA temp_store = MEMORY");
        exec("PRAGMA cache_size = -32000"); // ~32MB

        if (!exec(SCHEMA_SQL)) {
            return false;
        }

        // Prepare core statements once; bind/step/reset on every call.
        stmt_insert = prepare(
            "INSERT OR REPLACE INTO tx_index"
            " (hash, section_id, sender, receiver, token, type, timestamp, amount)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?)");

        stmt_find_by_hash = prepare(
            "SELECT hash, section_id, sender, receiver, token, type, timestamp, amount"
            " FROM tx_index WHERE hash = ? LIMIT 1");

        // actor-only queries use UNION of sender/receiver branches via two statements
        // plus merge in code; simpler and lets each branch hit its own index cleanly.
        stmt_find_sent = prepare(
            "SELECT hash, section_id, sender, receiver, token, type, timestamp, amount"
            " FROM tx_index WHERE sender = ? AND (? = 0 OR timestamp < ?)"
            " ORDER BY timestamp DESC LIMIT ?");
        stmt_find_sent_token = prepare(
            "SELECT hash, section_id, sender, receiver, token, type, timestamp, amount"
            " FROM tx_index WHERE sender = ? AND token = ? AND (? = 0 OR timestamp < ?)"
            " ORDER BY timestamp DESC LIMIT ?");

        stmt_find_recv = prepare(
            "SELECT hash, section_id, sender, receiver, token, type, timestamp, amount"
            " FROM tx_index WHERE receiver = ? AND (? = 0 OR timestamp < ?)"
            " ORDER BY timestamp DESC LIMIT ?");
        stmt_find_recv_token = prepare(
            "SELECT hash, section_id, sender, receiver, token, type, timestamp, amount"
            " FROM tx_index WHERE receiver = ? AND token = ? AND (? = 0 OR timestamp < ?)"
            " ORDER BY timestamp DESC LIMIT ?");

        stmt_row_count    = prepare("SELECT COUNT(*) FROM tx_index");
        stmt_last_section = prepare("SELECT COALESCE(MAX(section_id), -1) FROM tx_index");

        return stmt_insert && stmt_find_by_hash && stmt_find_sent && stmt_find_sent_token
            && stmt_find_recv && stmt_find_recv_token && stmt_row_count && stmt_last_section;
    }

    // In Light mode, restrict index to tx involving local wallets. In Full mode,
    // always true — we want to answer arbitrary queries for explorer/peers.
    bool should_index(const Transaction &tx) const {
        if (!node) return true;
        auto *dag = node->dag();
        if (!dag || dag->mode() == DagMode::Full) return true;

        auto *ac = node->account_controller();
        if (!ac) return true;
        auto actors = ac->accounts_ids();
        for (const auto &a : actors) {
            if (tx.sender() == a || tx.receiver() == a) return true;
        }
        return false;
    }

    static ChainIndexEntry row_to_entry(sqlite3_stmt *s) {
        ChainIndexEntry e;
        auto col_text = [&](int i) {
            const unsigned char *p = sqlite3_column_text(s, i);
            return p ? std::string(reinterpret_cast<const char *>(p)) : std::string();
        };
        e.hash      = col_text(0);
        e.section_id = SectionId(static_cast<long long>(sqlite3_column_int64(s, 1)));
        e.sender    = col_text(2);
        e.receiver  = col_text(3);
        e.token     = col_text(4);
        e.type      = sqlite3_column_int(s, 5);
        e.timestamp = static_cast<std::uint64_t>(sqlite3_column_int64(s, 6));
        e.amount    = col_text(7);
        return e;
    }

    void insert_tx(const Transaction &tx, const SectionId &section_id) {
        if (!should_index(tx)) return;
        auto *s = stmt_insert;
        sqlite3_reset(s);

        std::string hash     = tx.hash();
        std::string sender   = tx.sender().to_string();
        std::string receiver = tx.receiver().to_string();
        std::string token    = tx.token().to_string();
        std::string amount   = tx.amount().to_string();

        sqlite3_bind_text(s, 1, hash.c_str(),     -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(s, 2, static_cast<sqlite3_int64>(section_to_u64(section_id)));
        sqlite3_bind_text(s, 3, sender.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 4, receiver.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 5, token.c_str(),    -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(s, 6,  static_cast<int>(tx.type()));
        sqlite3_bind_int64(s, 7, static_cast<sqlite3_int64>(tx.timestamp()));
        sqlite3_bind_text(s, 8, amount.c_str(),   -1, SQLITE_TRANSIENT);

        if (sqlite3_step(s) != SQLITE_DONE) {
            eWarning("[ChainIndex] insert failed for tx {}: {}", hash, sqlite3_errmsg(db));
        }
    }

    std::vector<ChainIndexEntry> run_select_one_side(sqlite3_stmt *no_token,
                                                     sqlite3_stmt *with_token,
                                                     const std::string &actor,
                                                     const std::string &token,
                                                     std::uint64_t      before_ts,
                                                     int                limit) const {
        std::vector<ChainIndexEntry> out;
        out.reserve(limit);

        sqlite3_stmt *s = token.empty() ? no_token : with_token;
        sqlite3_reset(s);

        int idx = 1;
        sqlite3_bind_text(s, idx++, actor.c_str(), -1, SQLITE_TRANSIENT);
        if (!token.empty()) {
            sqlite3_bind_text(s, idx++, token.c_str(), -1, SQLITE_TRANSIENT);
        }
        // `(? = 0 OR timestamp < ?)` — two binds for the same ts; 0 means "no bound".
        sqlite3_bind_int64(s, idx++, static_cast<sqlite3_int64>(before_ts));
        sqlite3_bind_int64(s, idx++, static_cast<sqlite3_int64>(before_ts));
        sqlite3_bind_int(s, idx++, limit);

        while (sqlite3_step(s) == SQLITE_ROW) {
            out.push_back(row_to_entry(s));
        }
        return out;
    }
};

ChainIndex::ChainIndex(ExtraChainNode *node) : impl_(std::make_unique<Impl>()) {
    impl_->node = node;
    if (!impl_->open()) {
        eCritical("[ChainIndex] Failed to initialize index database");
    }
}

ChainIndex::~ChainIndex() = default;

void ChainIndex::on_section_written(const Section &s) {
    if (!impl_->db) return;

    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    impl_->exec("BEGIN IMMEDIATE");
    for (const auto &tx : s.transactions) {
        impl_->insert_tx(tx, s.id);
    }
    impl_->exec("COMMIT");
}

std::optional<ChainIndexEntry> ChainIndex::find_by_hash(const std::string &hash) const {
    if (!impl_->db || !impl_->stmt_find_by_hash) return std::nullopt;

    auto *s = impl_->stmt_find_by_hash;
    sqlite3_reset(s);
    sqlite3_bind_text(s, 1, hash.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(s) == SQLITE_ROW) {
        return Impl::row_to_entry(s);
    }
    return std::nullopt;
}

std::vector<ChainIndexEntry>
ChainIndex::find_sent_by(const std::string &actor, const std::string &token,
                         std::uint64_t before_timestamp, int limit) const {
    if (!impl_->db) return {};
    return impl_->run_select_one_side(impl_->stmt_find_sent, impl_->stmt_find_sent_token,
                                      actor, token, before_timestamp, limit);
}

std::vector<ChainIndexEntry>
ChainIndex::find_received_by(const std::string &actor, const std::string &token,
                             std::uint64_t before_timestamp, int limit) const {
    if (!impl_->db) return {};
    return impl_->run_select_one_side(impl_->stmt_find_recv, impl_->stmt_find_recv_token,
                                      actor, token, before_timestamp, limit);
}

std::vector<ChainIndexEntry>
ChainIndex::find_for_actor(const std::string &actor, const std::string &token,
                           std::uint64_t before_timestamp, int limit) const {
    if (!impl_->db) return {};

    // Two index-scans then merge in memory: each branch hits its own compound index
    // (sender,timestamp) / (receiver,timestamp), which is faster than a single OR
    // query that SQLite often can't plan with multiple indexes.
    auto sent = find_sent_by(actor, token, before_timestamp, limit);
    auto recv = find_received_by(actor, token, before_timestamp, limit);

    std::vector<ChainIndexEntry> merged;
    merged.reserve(sent.size() + recv.size());
    merged.insert(merged.end(), sent.begin(), sent.end());
    merged.insert(merged.end(), recv.begin(), recv.end());

    std::sort(merged.begin(), merged.end(),
              [](const ChainIndexEntry &a, const ChainIndexEntry &b) {
                  return a.timestamp > b.timestamp;
              });
    // Deduplicate self-transfers (sender == receiver).
    std::vector<ChainIndexEntry> out;
    out.reserve(merged.size());
    for (auto &e : merged) {
        if (!out.empty() && out.back().hash == e.hash) continue;
        out.push_back(std::move(e));
        if (static_cast<int>(out.size()) >= limit) break;
    }
    return out;
}

void ChainIndex::rebuild_from_disk() {
    if (!impl_->db || !impl_->node) return;

    auto *dag = impl_->node->dag();
    if (!dag) return;

    eLog("[ChainIndex] Rebuild started");

    std::lock_guard<std::mutex> lock(impl_->write_mutex);

    // Bulk-load pragmas. Restore safe ones at the end even on early return.
    impl_->exec("PRAGMA journal_mode = MEMORY");
    impl_->exec("PRAGMA synchronous = OFF");
    struct Restore {
        Impl *i;
        ~Restore() {
            i->exec("PRAGMA synchronous = NORMAL");
            i->exec("PRAGMA journal_mode = WAL");
        }
    } restore_on_exit { impl_.get() };

    impl_->exec("DELETE FROM tx_index");
    impl_->exec("BEGIN IMMEDIATE");

    const SectionId first = dag->first_saved_section();
    const SectionId last  = dag->current_section();
    std::uint64_t   count = 0;

    for (SectionId i = first; i <= last; i = i + 1) {
        auto section = dag->read_section(i);
        if (!section.has_value()) continue;
        for (const auto &tx : section->transactions) {
            impl_->insert_tx(tx, section->id);
            count++;
            if (count % 10000 == 0) {
                impl_->exec("COMMIT");
                impl_->exec("BEGIN IMMEDIATE");
                eLog("[ChainIndex] Rebuild progress: {} tx processed", count);
            }
        }
    }
    impl_->exec("COMMIT");
    impl_->exec("ANALYZE");

    eLog("[ChainIndex] Rebuild done, {} tx", count);
}

void ChainIndex::clear() {
    if (!impl_->db) return;
    std::lock_guard<std::mutex> lock(impl_->write_mutex);
    impl_->exec("DELETE FROM tx_index");
}

std::uint64_t ChainIndex::row_count() const {
    if (!impl_->db || !impl_->stmt_row_count) return 0;
    auto *s = impl_->stmt_row_count;
    sqlite3_reset(s);
    if (sqlite3_step(s) == SQLITE_ROW) {
        return static_cast<std::uint64_t>(sqlite3_column_int64(s, 0));
    }
    return 0;
}

SectionId ChainIndex::last_indexed_section() const {
    if (!impl_->db || !impl_->stmt_last_section) return SectionId(-1);
    auto *s = impl_->stmt_last_section;
    sqlite3_reset(s);
    if (sqlite3_step(s) == SQLITE_ROW) {
        return SectionId(static_cast<long long>(sqlite3_column_int64(s, 0)));
    }
    return SectionId(-1);
}
