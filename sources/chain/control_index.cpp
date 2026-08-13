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

#include "chain/control_index.h"

#include <sqlite3.h>

#include <filesystem>
#include <mutex>

#include "chain/dag.h"
#include "core/extrachain_node.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"

namespace {

    constexpr const char *DB_FILENAME = "Control.db";

    constexpr const char *SCHEMA_SQL = R"(
    CREATE TABLE IF NOT EXISTS control_index (
        section INTEGER PRIMARY KEY,
        hash    TEXT NOT NULL
    );
)";

    std::uint64_t section_to_u64(const SectionId &id) {
        auto i = id.to_int();
        if (i.has_value() && *i >= 0)
            return static_cast<std::uint64_t>(*i);
        try {
            return std::stoull(id.to_string());
        } catch (...) {
            return 0;
        }
    }

} // namespace

struct ControlIndex::Impl {
    ExtraChain::Core::ExtraChainNode *node = nullptr;
    sqlite3        *db   = nullptr;

    sqlite3_stmt *stmt_put      = nullptr;
    sqlite3_stmt *stmt_erase    = nullptr;
    sqlite3_stmt *stmt_get      = nullptr;
    sqlite3_stmt *stmt_last     = nullptr;
    sqlite3_stmt *stmt_count    = nullptr;
    sqlite3_stmt *stmt_count_to = nullptr;

    mutable std::mutex mutex;
    bool               rebuild_required = true;

    ~Impl() {
        auto finalize = [](sqlite3_stmt *&s) {
            if (s)
                sqlite3_finalize(s);
            s = nullptr;
        };
        finalize(stmt_put);
        finalize(stmt_erase);
        finalize(stmt_get);
        finalize(stmt_last);
        finalize(stmt_count);
        finalize(stmt_count_to);
        if (db) {
            exec(
                "INSERT INTO control_meta(key,value) VALUES('clean_shutdown',1)"
                " ON CONFLICT(key) DO UPDATE SET value=excluded.value");
            sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr);
            sqlite3_close(db);
        }
    }

    bool exec(const char *sql) {
        char *err = nullptr;
        if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
            eWarning("[ControlIndex] exec failed: {} | sql: {}", err ? err : "?", sql);
            if (err)
                sqlite3_free(err);
            return false;
        }
        return true;
    }

    sqlite3_stmt *prepare(const char *sql) {
        sqlite3_stmt *s = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) {
            eWarning("[ControlIndex] prepare failed: {} | sql: {}", sqlite3_errmsg(db), sql);
            return nullptr;
        }
        return s;
    }

    void put_unlocked(const SectionId &section_id, const std::string &hash) {
        sqlite3_reset(stmt_put);
        sqlite3_clear_bindings(stmt_put);
        sqlite3_bind_int64(stmt_put, 1, static_cast<sqlite3_int64>(section_to_u64(section_id)));
        sqlite3_bind_text(stmt_put, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt_put) != SQLITE_DONE) {
            eWarning("[ControlIndex] put failed: {}", sqlite3_errmsg(db));
        }
    }

    bool open() {
        std::filesystem::create_directories(ChainConst::DAG_CACHE_FOLDER);
        auto path = ChainConst::DAG_CACHE_FOLDER + "/" + DB_FILENAME;

        if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
            eWarning("[ControlIndex] Failed to open {}: {}", path, sqlite3_errmsg(db));
            sqlite3_close(db);
            db = nullptr;
            return false;
        }

        exec("PRAGMA journal_mode = WAL");
        exec("PRAGMA synchronous = NORMAL");
        exec("PRAGMA temp_store = MEMORY");
        const auto full_node = node == nullptr || node->runtime_profile() == RuntimeProfile::FullNode;
        sqlite3_wal_autocheckpoint(db, full_node ? 4096 : 1024);
        exec(full_node ? "PRAGMA journal_size_limit = 67108864" : "PRAGMA journal_size_limit = 16777216");

        if (!exec(SCHEMA_SQL))
            return false;
        if (!exec("CREATE TABLE IF NOT EXISTS control_meta ("
                  "key TEXT PRIMARY KEY, value INTEGER NOT NULL) WITHOUT ROWID"))
            return false;

        sqlite3_stmt *clean_state = prepare("SELECT value FROM control_meta WHERE key='clean_shutdown'");
        rebuild_required          = clean_state == nullptr || sqlite3_step(clean_state) != SQLITE_ROW
                           || sqlite3_column_int(clean_state, 0) != 1;
        if (clean_state != nullptr)
            sqlite3_finalize(clean_state);
        if (!exec("INSERT INTO control_meta(key,value) VALUES('clean_shutdown',0)"
                  " ON CONFLICT(key) DO UPDATE SET value=excluded.value"))
            return false;

        stmt_put = prepare(
            "INSERT INTO control_index (section, hash) VALUES (?, ?)"
            " ON CONFLICT(section) DO UPDATE SET hash = excluded.hash");
        stmt_erase = prepare("DELETE FROM control_index WHERE section = ?");
        stmt_get   = prepare("SELECT hash FROM control_index WHERE section = ?");
        stmt_last  = prepare(
            "SELECT section, hash FROM control_index WHERE section <= ?"
             " ORDER BY section DESC LIMIT 1");
        stmt_count    = prepare("SELECT COUNT(*) FROM control_index");
        stmt_count_to = prepare("SELECT COUNT(*) FROM control_index WHERE section <= ?");

        return stmt_put && stmt_erase && stmt_get && stmt_last && stmt_count && stmt_count_to;
    }
};

ControlIndex::ControlIndex(ExtraChain::Core::ExtraChainNode *node)
    : impl_(std::make_unique<Impl>()) {
    impl_->node = node;
    if (!impl_->open()) {
        eWarning("[ControlIndex] open failed; control lookups fall back to section scan");
    }
}

ControlIndex::~ControlIndex() = default;

void ControlIndex::put(const SectionId &section_id, const std::string &hash) {
    if (!impl_->db || !impl_->stmt_put)
        return;
    std::lock_guard lock(impl_->mutex);
    impl_->put_unlocked(section_id, hash);
}

void ControlIndex::erase(const SectionId &section_id) {
    if (!impl_->db || !impl_->stmt_erase)
        return;
    std::lock_guard lock(impl_->mutex);
    sqlite3_reset(impl_->stmt_erase);
    sqlite3_bind_int64(impl_->stmt_erase, 1, static_cast<sqlite3_int64>(section_to_u64(section_id)));
    sqlite3_step(impl_->stmt_erase);
}

std::optional<std::string> ControlIndex::get(const SectionId &section_id) const {
    if (!impl_->db || !impl_->stmt_get)
        return std::nullopt;
    std::lock_guard lock(impl_->mutex);
    sqlite3_reset(impl_->stmt_get);
    sqlite3_bind_int64(impl_->stmt_get, 1, static_cast<sqlite3_int64>(section_to_u64(section_id)));
    if (sqlite3_step(impl_->stmt_get) != SQLITE_ROW) {
        sqlite3_reset(impl_->stmt_get);
        return std::nullopt;
    }
    auto *txt = reinterpret_cast<const char *>(sqlite3_column_text(impl_->stmt_get, 0));
    if (!txt) {
        sqlite3_reset(impl_->stmt_get);
        return std::nullopt;
    }
    std::string result(txt);
    sqlite3_reset(impl_->stmt_get);
    return result;
}

std::optional<std::pair<SectionId, std::string>> ControlIndex::last_at_or_below(const SectionId &from) const {
    if (!impl_->db || !impl_->stmt_last)
        return std::nullopt;
    std::lock_guard lock(impl_->mutex);
    sqlite3_reset(impl_->stmt_last);
    // from < 0 means "from the top": use the max representable section bound.
    auto          fi     = from.to_int();
    bool          is_top = !fi.has_value() || *fi < 0;
    std::uint64_t bound  = is_top ? static_cast<std::uint64_t>(INT64_MAX) : section_to_u64(from);
    sqlite3_bind_int64(impl_->stmt_last, 1, static_cast<sqlite3_int64>(bound));
    if (sqlite3_step(impl_->stmt_last) != SQLITE_ROW) {
        sqlite3_reset(impl_->stmt_last);
        return std::nullopt;
    }
    auto  sec = static_cast<std::uint64_t>(sqlite3_column_int64(impl_->stmt_last, 0));
    auto *txt = reinterpret_cast<const char *>(sqlite3_column_text(impl_->stmt_last, 1));
    if (!txt) {
        sqlite3_reset(impl_->stmt_last);
        return std::nullopt;
    }
    std::pair result { SectionId(static_cast<long long>(sec)), std::string(txt) };
    sqlite3_reset(impl_->stmt_last);
    return result;
}

void ControlIndex::clear() {
    if (!impl_->db)
        return;
    std::lock_guard lock(impl_->mutex);
    impl_->exec("DELETE FROM control_index");
}

std::uint64_t ControlIndex::row_count() const {
    if (!impl_->db || !impl_->stmt_count)
        return 0;
    std::lock_guard lock(impl_->mutex);
    sqlite3_reset(impl_->stmt_count);
    if (sqlite3_step(impl_->stmt_count) != SQLITE_ROW) {
        sqlite3_reset(impl_->stmt_count);
        return 0;
    }
    const auto result = static_cast<std::uint64_t>(sqlite3_column_int64(impl_->stmt_count, 0));
    sqlite3_reset(impl_->stmt_count);
    return result;
}

std::uint64_t ControlIndex::row_count_at_or_below(const SectionId &section_id) const {
    if (!impl_->db || !impl_->stmt_count_to)
        return 0;
    std::lock_guard lock(impl_->mutex);
    sqlite3_reset(impl_->stmt_count_to);
    sqlite3_clear_bindings(impl_->stmt_count_to);
    sqlite3_bind_int64(impl_->stmt_count_to, 1, static_cast<sqlite3_int64>(section_to_u64(section_id)));
    if (sqlite3_step(impl_->stmt_count_to) != SQLITE_ROW) {
        sqlite3_reset(impl_->stmt_count_to);
        return 0;
    }
    const auto result = static_cast<std::uint64_t>(sqlite3_column_int64(impl_->stmt_count_to, 0));
    sqlite3_reset(impl_->stmt_count_to);
    return result;
}

bool ControlIndex::rebuild_required() const {
    std::lock_guard lock(impl_->mutex);
    return impl_->rebuild_required;
}

void ControlIndex::rebuild_from_dag() {
    if (!impl_->db || !impl_->node)
        return;

    auto *dag = impl_->node->dag();
    if (!dag)
        return;

    // Walk control-aligned sections and copy their hashes into the index. Reads
    // go through Dag::read_section (pack + hot aware). One transaction for speed.
    std::lock_guard lock(impl_->mutex);
    if (!impl_->exec("BEGIN IMMEDIATE"))
        return;
    if (!impl_->exec("DELETE FROM control_index")) {
        impl_->exec("ROLLBACK");
        return;
    }

    SectionId     first = dag->first_saved_section();
    SectionId     last  = dag->current_section();
    std::uint64_t added = 0;
    for (SectionId i = (first < SectionId(0) ? SectionId(0) : first); i <= last; i += CONTROL_INTERVAL_MOD) {
        // Read the section directly (not read_control, which consults this index).
        auto section = dag->read_section(i);
        if (section.has_value() && section->control.has_value()) {
            impl_->put_unlocked(i, section->control.value());
            ++added;
        }
    }

    if (!impl_->exec("COMMIT")) {
        impl_->exec("ROLLBACK");
        return;
    }
    impl_->rebuild_required = false;
    eLog("[ControlIndex] rebuilt: {} control points [{}..{}]", added, first, last);
}
