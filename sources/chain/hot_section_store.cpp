/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "chain/hot_section_store.h"

#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <mutex>

#include "utils/exc_logs.h"

namespace {

    std::optional<sqlite3_int64> section_to_i64(const SectionId &section) {
        auto value = section.to_int();
        if (!value.has_value() || *value < 0)
            return std::nullopt;
        return static_cast<sqlite3_int64>(*value);
    }

} // namespace

struct HotSectionStore::Impl {
    sqlite3           *db               = nullptr;
    sqlite3_stmt      *put_stmt         = nullptr;
    sqlite3_stmt      *get_stmt         = nullptr;
    sqlite3_stmt      *range_stmt       = nullptr;
    sqlite3_stmt      *bounds_stmt      = nullptr;
    sqlite3_stmt      *meta_put_stmt    = nullptr;
    sqlite3_stmt      *meta_get_stmt    = nullptr;
    sqlite3_stmt      *erase_range_stmt = nullptr;
    sqlite3_stmt      *erase_from_stmt  = nullptr;
    mutable std::mutex mutex;

    ~Impl() {
        sqlite3_finalize(put_stmt);
        sqlite3_finalize(get_stmt);
        sqlite3_finalize(range_stmt);
        sqlite3_finalize(bounds_stmt);
        sqlite3_finalize(meta_put_stmt);
        sqlite3_finalize(meta_get_stmt);
        sqlite3_finalize(erase_range_stmt);
        sqlite3_finalize(erase_from_stmt);
        if (db)
            sqlite3_close(db);
    }

    bool exec(const char *sql) {
        char     *error  = nullptr;
        const int result = sqlite3_exec(db, sql, nullptr, nullptr, &error);
        if (result == SQLITE_OK)
            return true;
        eWarning("[HotSectionStore] SQL error: {}", error ? error : sqlite3_errmsg(db));
        sqlite3_free(error);
        return false;
    }

    sqlite3_stmt *prepare(const char *sql) {
        sqlite3_stmt *statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK)
            return statement;
        eWarning("[HotSectionStore] prepare failed: {}", sqlite3_errmsg(db));
        return nullptr;
    }

    bool put_meta(const char *key, sqlite3_int64 value) {
        sqlite3_reset(meta_put_stmt);
        sqlite3_clear_bindings(meta_put_stmt);
        sqlite3_bind_text(meta_put_stmt, 1, key, -1, SQLITE_STATIC);
        sqlite3_bind_int64(meta_put_stmt, 2, value);
        return sqlite3_step(meta_put_stmt) == SQLITE_DONE;
    }

    std::optional<sqlite3_int64> get_meta(const char *key) {
        sqlite3_reset(meta_get_stmt);
        sqlite3_clear_bindings(meta_get_stmt);
        sqlite3_bind_text(meta_get_stmt, 1, key, -1, SQLITE_STATIC);
        if (sqlite3_step(meta_get_stmt) != SQLITE_ROW)
            return std::nullopt;
        return sqlite3_column_int64(meta_get_stmt, 0);
    }

    bool open(const std::filesystem::path &path) {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            eWarning("[HotSectionStore] cannot create directory: {}", error.message());
            return false;
        }

        if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) {
            eWarning("[HotSectionStore] cannot open {}: {}", path.string(), sqlite3_errmsg(db));
            if (db) {
                sqlite3_close(db);
                db = nullptr;
            }
            return false;
        }

        sqlite3_busy_timeout(db, 5000);
        if (!exec("PRAGMA journal_mode=WAL") || !exec("PRAGMA synchronous=NORMAL")
            || !exec("PRAGMA temp_store=MEMORY")
            || !exec("CREATE TABLE IF NOT EXISTS sections ("
                     "section INTEGER PRIMARY KEY, payload BLOB NOT NULL) WITHOUT ROWID")
            || !exec("CREATE TABLE IF NOT EXISTS chain_meta ("
                     "key TEXT PRIMARY KEY, value INTEGER NOT NULL) WITHOUT ROWID")) {
            return false;
        }

        put_stmt = prepare(
            "INSERT INTO sections(section,payload) VALUES(?1,?2) "
            "ON CONFLICT(section) DO UPDATE SET payload=excluded.payload");
        get_stmt   = prepare("SELECT payload FROM sections WHERE section=?1");
        range_stmt = prepare(
            "SELECT section,payload FROM sections "
            "WHERE section>=?1 AND section<=?2 ORDER BY section");
        bounds_stmt   = prepare("SELECT MIN(section),MAX(section) FROM sections");
        meta_put_stmt = prepare(
            "INSERT INTO chain_meta(key,value) VALUES(?1,?2)"
            " ON CONFLICT(key) DO UPDATE SET value=excluded.value");
        meta_get_stmt    = prepare("SELECT value FROM chain_meta WHERE key=?1");
        erase_range_stmt = prepare("DELETE FROM sections WHERE section>=?1 AND section<=?2");
        erase_from_stmt  = prepare("DELETE FROM sections WHERE section>=?1");
        return put_stmt && get_stmt && range_stmt && bounds_stmt && meta_put_stmt && meta_get_stmt
               && erase_range_stmt && erase_from_stmt;
    }
};

HotSectionStore::HotSectionStore(const std::filesystem::path &path)
    : impl_(std::make_unique<Impl>()) {
    if (!impl_->open(path))
        eCritical("[HotSectionStore] initialization failed");
}

HotSectionStore::~HotSectionStore() = default;

bool HotSectionStore::is_open() const {
    return impl_->db && impl_->put_stmt && impl_->get_stmt && impl_->range_stmt && impl_->bounds_stmt
           && impl_->meta_put_stmt && impl_->meta_get_stmt && impl_->erase_range_stmt && impl_->erase_from_stmt;
}

bool HotSectionStore::put(const SectionId &section, const std::string &payload) {
    const auto id = section_to_i64(section);
    if (!is_open() || !id.has_value() || payload.empty())
        return false;
    std::lock_guard lock(impl_->mutex);
    sqlite3_reset(impl_->put_stmt);
    sqlite3_clear_bindings(impl_->put_stmt);
    sqlite3_bind_int64(impl_->put_stmt, 1, *id);
    sqlite3_bind_blob(impl_->put_stmt, 2, payload.data(), static_cast<int>(payload.size()), SQLITE_STATIC);
    const bool stored = sqlite3_step(impl_->put_stmt) == SQLITE_DONE;
    if (!stored)
        eWarning("[HotSectionStore] write failed: {}", sqlite3_errmsg(impl_->db));
    sqlite3_reset(impl_->put_stmt);
    sqlite3_clear_bindings(impl_->put_stmt);
    return stored;
}

bool HotSectionStore::put_many(const std::map<SectionId, std::string> &sections) {
    return commit_batch(sections, std::nullopt);
}

bool HotSectionStore::commit_batch(const std::map<SectionId, std::string>               &sections,
                                   const std::optional<std::pair<SectionId, SectionId>> &committed_range) {
    if (!is_open() || sections.empty())
        return false;
    std::lock_guard lock(impl_->mutex);
    if (!impl_->exec("BEGIN IMMEDIATE"))
        return false;

    for (const auto &[section, payload] : sections) {
        const auto id = section_to_i64(section);
        if (!id.has_value() || payload.empty()) {
            impl_->exec("ROLLBACK");
            return false;
        }
        sqlite3_reset(impl_->put_stmt);
        sqlite3_clear_bindings(impl_->put_stmt);
        sqlite3_bind_int64(impl_->put_stmt, 1, *id);
        sqlite3_bind_blob(impl_->put_stmt, 2, payload.data(), static_cast<int>(payload.size()), SQLITE_STATIC);
        if (sqlite3_step(impl_->put_stmt) != SQLITE_DONE) {
            eWarning("[HotSectionStore] batch write failed: {}", sqlite3_errmsg(impl_->db));
            sqlite3_reset(impl_->put_stmt);
            sqlite3_clear_bindings(impl_->put_stmt);
            impl_->exec("ROLLBACK");
            return false;
        }
    }
    sqlite3_reset(impl_->put_stmt);
    sqlite3_clear_bindings(impl_->put_stmt);

    if (committed_range.has_value()) {
        const auto first = section_to_i64(committed_range->first);
        const auto last  = section_to_i64(committed_range->second);
        if (!first.has_value() || !last.has_value() || *last < *first) {
            impl_->exec("ROLLBACK");
            return false;
        }
        if (!impl_->put_meta("committed_first", *first) || !impl_->put_meta("committed_last", *last)) {
            sqlite3_reset(impl_->meta_put_stmt);
            impl_->exec("ROLLBACK");
            return false;
        }
        sqlite3_reset(impl_->meta_put_stmt);
    }
    if (impl_->exec("COMMIT"))
        return true;
    impl_->exec("ROLLBACK");
    return false;
}

std::optional<std::pair<SectionId, SectionId>> HotSectionStore::committed_range() const {
    if (!is_open())
        return std::nullopt;
    std::lock_guard lock(impl_->mutex);
    const auto      first = impl_->get_meta("committed_first");
    const auto      last  = impl_->get_meta("committed_last");
    sqlite3_reset(impl_->meta_get_stmt);
    if (!first.has_value() || !last.has_value() || *first < 0 || *last < *first)
        return std::nullopt;
    return std::pair { SectionId(static_cast<long long>(*first)), SectionId(static_cast<long long>(*last)) };
}

std::optional<std::string> HotSectionStore::get(const SectionId &section) const {
    const auto id = section_to_i64(section);
    if (!is_open() || !id.has_value())
        return std::nullopt;
    std::lock_guard lock(impl_->mutex);
    sqlite3_reset(impl_->get_stmt);
    sqlite3_clear_bindings(impl_->get_stmt);
    sqlite3_bind_int64(impl_->get_stmt, 1, *id);
    std::optional<std::string> payload;
    if (sqlite3_step(impl_->get_stmt) == SQLITE_ROW) {
        const auto *data = static_cast<const char *>(sqlite3_column_blob(impl_->get_stmt, 0));
        const int   size = sqlite3_column_bytes(impl_->get_stmt, 0);
        if (data && size > 0)
            payload.emplace(data, static_cast<std::size_t>(size));
    }
    sqlite3_reset(impl_->get_stmt);
    return payload;
}

bool HotSectionStore::contains(const SectionId &section) const {
    return get(section).has_value();
}

std::map<SectionId, std::string> HotSectionStore::read_range(const SectionId &from, const SectionId &to) const {
    std::map<SectionId, std::string> result;
    const auto                       first = section_to_i64(from);
    const auto                       last  = section_to_i64(to);
    if (!is_open() || !first.has_value() || !last.has_value() || *first > *last)
        return result;

    std::lock_guard lock(impl_->mutex);
    sqlite3_reset(impl_->range_stmt);
    sqlite3_clear_bindings(impl_->range_stmt);
    sqlite3_bind_int64(impl_->range_stmt, 1, *first);
    sqlite3_bind_int64(impl_->range_stmt, 2, *last);
    while (sqlite3_step(impl_->range_stmt) == SQLITE_ROW) {
        const auto  section = sqlite3_column_int64(impl_->range_stmt, 0);
        const auto *data    = static_cast<const char *>(sqlite3_column_blob(impl_->range_stmt, 1));
        const int   size    = sqlite3_column_bytes(impl_->range_stmt, 1);
        if (section >= 0 && data && size > 0) {
            result.emplace(SectionId(static_cast<long long>(section)),
                           std::string(data, static_cast<std::size_t>(size)));
        }
    }
    sqlite3_reset(impl_->range_stmt);
    return result;
}

std::optional<std::pair<SectionId, SectionId>> HotSectionStore::bounds() const {
    if (!is_open() || !impl_->bounds_stmt)
        return std::nullopt;
    std::lock_guard lock(impl_->mutex);
    sqlite3_reset(impl_->bounds_stmt);
    const bool has_row = sqlite3_step(impl_->bounds_stmt) == SQLITE_ROW;
    if (!has_row || sqlite3_column_type(impl_->bounds_stmt, 0) == SQLITE_NULL
        || sqlite3_column_type(impl_->bounds_stmt, 1) == SQLITE_NULL) {
        sqlite3_reset(impl_->bounds_stmt);
        return std::nullopt;
    }
    const auto first = sqlite3_column_int64(impl_->bounds_stmt, 0);
    const auto last  = sqlite3_column_int64(impl_->bounds_stmt, 1);
    sqlite3_reset(impl_->bounds_stmt);
    if (first < 0 || last < first)
        return std::nullopt;
    return std::pair { SectionId(static_cast<long long>(first)), SectionId(static_cast<long long>(last)) };
}

bool HotSectionStore::erase_range(const SectionId &from, const SectionId &to) {
    const auto first = section_to_i64(from);
    const auto last  = section_to_i64(to);
    if (!is_open() || !first.has_value() || !last.has_value() || *first > *last)
        return false;
    std::lock_guard lock(impl_->mutex);
    sqlite3_reset(impl_->erase_range_stmt);
    sqlite3_clear_bindings(impl_->erase_range_stmt);
    sqlite3_bind_int64(impl_->erase_range_stmt, 1, *first);
    sqlite3_bind_int64(impl_->erase_range_stmt, 2, *last);
    const bool erased = sqlite3_step(impl_->erase_range_stmt) == SQLITE_DONE;
    sqlite3_reset(impl_->erase_range_stmt);
    return erased;
}

bool HotSectionStore::erase_from(const SectionId &from) {
    const auto first = section_to_i64(from);
    if (!is_open() || !first.has_value())
        return false;
    std::lock_guard lock(impl_->mutex);
    if (!impl_->exec("BEGIN IMMEDIATE"))
        return false;

    sqlite3_reset(impl_->erase_from_stmt);
    sqlite3_clear_bindings(impl_->erase_from_stmt);
    sqlite3_bind_int64(impl_->erase_from_stmt, 1, *first);
    const bool erased = sqlite3_step(impl_->erase_from_stmt) == SQLITE_DONE;
    sqlite3_reset(impl_->erase_from_stmt);
    if (!erased) {
        impl_->exec("ROLLBACK");
        return false;
    }

    const auto committed_first = impl_->get_meta("committed_first");
    const auto committed_last  = impl_->get_meta("committed_last");
    sqlite3_reset(impl_->meta_get_stmt);
    if (committed_first.has_value() && committed_last.has_value()) {
        const auto new_last  = std::min(*committed_last, *first);
        const auto new_first = std::min(*committed_first, new_last);
        if (!impl_->put_meta("committed_first", new_first) || !impl_->put_meta("committed_last", new_last)) {
            sqlite3_reset(impl_->meta_put_stmt);
            impl_->exec("ROLLBACK");
            return false;
        }
        sqlite3_reset(impl_->meta_put_stmt);
    }

    if (impl_->exec("COMMIT"))
        return true;
    impl_->exec("ROLLBACK");
    return false;
}

bool HotSectionStore::clear() {
    if (!is_open())
        return false;
    std::lock_guard lock(impl_->mutex);
    if (!impl_->exec("BEGIN IMMEDIATE"))
        return false;
    if (impl_->exec("DELETE FROM sections") && impl_->exec("DELETE FROM chain_meta") && impl_->exec("COMMIT"))
        return true;
    impl_->exec("ROLLBACK");
    return false;
}
