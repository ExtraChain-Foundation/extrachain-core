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

#include "utils/db_connector.h"

#include "sqlite3.h"
#include <blake3.h>
#include <cctype>
#include <filesystem>

#include "utils/exc_logs.h"
#include "utils/file_io.h"
#include "utils/legacy_compression.h"

// #define ENABLE_SQLITE_TRUE_LOGS

namespace {
    constexpr std::size_t MAX_LEGACY_DATABASE_BYTES = 512U * 1024U * 1024U;
}

DbConnector::DbConnector(const std::string &filePath, DbConnectorType type) {
    if (filePath.empty()) {
        eFatal("[DbConnector] Empty file name");
    }

    if (type == DbConnectorType::Compressed) {
        m_type       = type;
        this->m_file = filePath + ".temp"; // TODO: + random str?

        if (!std::filesystem::exists(filePath))
            return;
        const auto compressed = FileIo::read_all(filePath);
        if (!compressed.has_value()) {
            eFatal("[DbConnector] Can't open db file");
        }
        const auto uncompressed = LegacyCompression::decompress(*compressed, MAX_LEGACY_DATABASE_BYTES);
        if (!uncompressed.has_value() || !FileIo::write_atomic(this->m_file, *uncompressed).has_value()) {
            eFatal("[DbConnector] Can't decompress db file");
        }
        return;
    }

    this->m_file = filePath;
}

DbConnector::DbConnector(const std::filesystem::path &filePath, DbConnectorType type)
    : DbConnector(filePath.string(), type) {
}

DbConnector::DbConnector(const FsPath &filePath, DbConnectorType type)
    : DbConnector(filePath.native(), type) {
}

DbConnector::DbConnector(const char *filePath, DbConnectorType type)
    : DbConnector(std::string(filePath), type) {
}

DbConnector::DbConnector(DbConnector &&rhs) {
    if (this == &rhs)
        return;

    const std::scoped_lock lock(rhs.m_database_mutex);
    this->m_file = std::move(rhs.m_file);
    this->m_open = rhs.m_open;
    this->db     = rhs.db;
    this->m_type = rhs.m_type;
    rhs.m_open   = false;
    rhs.db       = nullptr;
}

DbConnector::~DbConnector() {
    // TODO: check if sqlite pointer is active
    if (db != nullptr) {
        close();
        // sqlite3_db_release_memory(db);
    }
    // close();
}

std::string DbConnector::sqlite_version() {
    return sqlite3_libversion();
}

bool DbConnector::open(bool create_if_missing) {
    const std::unique_lock lock(m_database_mutex);
    if (is_open()) {
        eFatal("[DbConnector] Double open");
        return false;
    }
    if (m_file.empty()) {
        eFatal("[DbConnector] File name empty");
        return false;
    }

    int flags = SQLITE_OPEN_READWRITE | (create_if_missing ? SQLITE_OPEN_CREATE : 0);
    int rc    = sqlite3_open_v2(m_file.c_str(), &db, flags, nullptr);
    if (rc) {
        // Missing file is an expected outcome for read-side opens: no empty db is created
        if (create_if_missing) {
            eWarning("[DbConnector] {}, failed to open database: {}", m_file, sqlite3_errmsg(db));
        }
        sqlite3_close_v2(db);
        db = nullptr;
        return false;
    } else {
        if (!std::filesystem::exists(m_file)) {
            // eFatal("[DbConnector] Open error: {}", m_file);
            return false;
        }

        // Without this sqlite returns SQLITE_BUSY the instant another connection holds
        // the write lock, and every caller here treats that as a plain failure: the row
        // is dropped, nothing retries, nothing is re-requested. On a six-node stand that
        // silently lost 11-24 vector rows per node — chat messages — while the network
        // layer was healthy and delivering (see docs/TODO.md 0.45). Waiting is free
        // compared with losing data; contention here is short-lived by nature.
        sqlite3_busy_timeout(db, 5000);

        m_open = true;
        return true;
    }
}

bool DbConnector::close() {
    const std::unique_lock lock(m_database_mutex);
    if (!m_open)
        return true;

    int rc = sqlite3_close_v2(db);
    if (rc) {
        eWarning("[DbConnector] {}", sqlite3_errmsg(db));
        return false;
    } else {
        m_open = false;

        if (std::filesystem::exists(m_file) && std::filesystem::file_size(m_file) == 0) {
            std::error_code error;
            std::filesystem::remove(m_file, error);
        }

        if (m_type == DbConnectorType::Compressed) {
            const auto data = FileIo::read_all(m_file);
            if (!data.has_value()) {
                eFatal("[DbConnector] Can't open database '{}'", m_file);
            }
            const auto compressed = LegacyCompression::compress(*data);
            const auto target     = m_file.substr(0, m_file.size() - std::string_view(".temp").size());
            if (!compressed.has_value() || !FileIo::write_atomic(target, *compressed).has_value()) {
                eFatal("[DbConnector] Can't compress database '{}'", m_file);
            }
            std::error_code error;
            std::filesystem::remove(m_file, error);
        }

        return true;
    }
}

std::vector<DbRow> DbConnector::select(std::string query, std::string tableName, DbRow binds) {
    // std::pair with status?
    std::unique_lock lock(m_database_mutex);
    if (!is_open()) {
        eFatal("[DbConnector] Database '{}' not open", m_file);
    }

    sqlite3_stmt      *stmt = nullptr;
    std::vector<DbRow> res;
    const auto         prepare_result = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (prepare_result != SQLITE_OK) {
        eWarning("[DbConnector] Select prepare error: {}", sqlite3_errmsg(db));
        return {};
    }

    if (!binds.empty()) {
        if (!implementation_prepare(tableName, binds, stmt)) {
            eWarning("[DbConnector] Select bind error");
            sqlite3_finalize(stmt);
            return {};
        }
    }

    int rs = sqlite3_step(stmt);

    while (rs == SQLITE_ROW) {
        DbRow row;
        int   colNum = sqlite3_column_count(stmt);

        for (int i = 0; i < colNum; i++) {
            std::string n = sqlite3_column_name(stmt, i);
            std::string t;
            switch (sqlite3_column_type(stmt, i)) {
            case SQLITE_BLOB: {
                int size = sqlite3_column_bytes(stmt, i);
                t        = std::string(reinterpret_cast<const char *>(sqlite3_column_blob(stmt, i)), size);
                break;
            }
            case SQLITE3_TEXT: {
                t = (reinterpret_cast<const char *>(sqlite3_column_text(stmt, i)));
                break;
            }
            case SQLITE_INTEGER:
                t = std::to_string(sqlite3_column_int64(stmt, i));
                break;
            case SQLITE_FLOAT:
                t = std::to_string(sqlite3_column_double(stmt, i));
                break;
            case SQLITE_NULL:
                continue;
            default:
                break;
            }

            row.insert({ n, t });
        }

        res.push_back(row);

        rs = sqlite3_step(stmt);
    }

    if (rs != SQLITE_DONE) {
        eWarning("[DbConnector] {} ({}) {} {} error: {}", m_file, m_type, query, tableName, sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return {};
    }

    sqlite3_finalize(stmt);
    return res;
}

std::vector<DbRow> DbConnector::select_all(std::string table, int limit) {
    std::string query =
        fmt::format("SELECT * FROM {}{}", table, limit > 0 ? " LIMIT " + std::to_string(limit) : "");
    return select(query);
}

std::unique_ptr<DbIterator> DbConnector::select_while(std::string query, std::string table_name, DbRow binds) {
    std::unique_lock lock(m_database_mutex);
    if (!is_open()) {
        eFatal("[DBConnector] Database {} not open", m_file);
    }

    sqlite3_stmt      *stmt = nullptr;
    std::vector<DbRow> res;
    const auto         prepare_result = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (prepare_result != SQLITE_OK) {
        eWarning("[DbConnector] Select iterator prepare error: {}", sqlite3_errmsg(db));
        return {};
    }

    if (!binds.empty()) {
        if (!implementation_prepare(table_name, binds, stmt)) {
            eWarning("[DbConnector] Select bind error");
            sqlite3_finalize(stmt);
            return {};
        }
    }

    return std::make_unique<DbIterator>(stmt);
}

bool DbConnector::insert(const std::string &tableName, const DbRow &data) {
    return this->implementation_insert(tableName, data, false);
}

bool DbConnector::replace(const std::string &tableName, const DbRow &data) {
    return this->implementation_insert(tableName, data, true);
}

bool DbConnector::update(const std::string &query) {
    return this->query(query);
}

bool DbConnector::update(const std::string &table_name, const DbRow &set_data, const DbRow &where_data) {
    std::unique_lock lock(m_database_mutex);
    if (!is_open()) {
        eFatal("[DbConnector] Database not open: {}", m_file);
    }
    if (set_data.size() == 0) {
        eWarning("[DbConnector] {}(false): [ImplementationUpdate] SET data is empty", file());
        return false;
    }
    if (where_data.size() == 0) {
        eWarning("[DbConnector] {}(false): [ImplementationUpdate] WHERE conditions are empty", file());
        return false;
    }

    std::string query = fmt::format("UPDATE {} SET ", table_name);
    std::string set_clause;
    for (auto &el : set_data) {
        set_clause += fmt::format("{} = ?, ", el.first);
    }
    set_clause.erase(set_clause.size() - 2, 2);
    query += set_clause;

    query += " WHERE ";
    std::string where_clause;
    for (auto &el : where_data) {
        where_clause += fmt::format("{} = ? AND ", el.first);
    }
    where_clause.erase(where_clause.size() - 5, 5);
    query += where_clause;

    sqlite3_stmt *stmt = nullptr;
    int           rc   = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        eWarning("[DbConnector] {}(false): {}", file(), query);
        eWarning("[DbConnector] ImplementationUpdate: prepare failed: {}", sqlite3_errmsg(db));
        return false;
    }
    int bind_index = 1;
    for (auto &el : set_data) {
        rc = sqlite3_bind_text(stmt, bind_index++, el.second.c_str(), -1, SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            sqlite3_finalize(stmt);
            return false;
        }
    }

    for (auto &el : where_data) {
        rc = sqlite3_bind_text(stmt, bind_index++, el.second.c_str(), -1, SQLITE_TRANSIENT);
        if (rc != SQLITE_OK) {
            sqlite3_finalize(stmt);
            return false;
        }
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        eWarning("[DbConnector] ImplementationUpdate: Execution failed: {}", sqlite3_errmsg(db));
        eWarning("[DbConnector] {}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        return false;
    }

    int changes = sqlite3_changes(db);
    if (changes == 0) {
        eWarning("[DbConnector] ImplementationUpdate: No rows affected: {}", sqlite3_errmsg(db));
        eWarning("[DbConnector] {}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        return false;
    }

#ifdef ENABLE_SQLITE_TRUE_LOGS
    eLog("[DbConnector] {}(true): {}", file(), query);
#endif

    sqlite3_finalize(stmt);
    return true;
}

bool DbConnector::create_table(const std::string &query) {
    std::string normalized;
    normalized.reserve(query.size());
    bool whitespace = false;
    for (const unsigned char character : query) {
        if (std::isspace(character)) {
            whitespace = !normalized.empty();
        } else {
            if (whitespace) {
                normalized.push_back(' ');
            }
            normalized.push_back(static_cast<char>(character));
            whitespace = false;
        }
    }
    return this->query(std::move(normalized));
}

std::expected<std::string, SqlCreateError> DbConnector::create_table(const DbSchema &query) {
    auto sql = query.to_sql();
    if (!sql.has_value())
        return sql;

    if (!create_table(sql.value())) {
        return std::unexpected(SqlCreateError::ExecutionFailed);
    }
    return sql;
}

bool DbConnector::create_table(const std::string &table_name, const std::vector<DBColumn> &columns) {
    if (columns.empty() || table_name.empty()) {
        return false;
    }

    std::stringstream query;
    query << "CREATE TABLE " << table_name << " (";

    for (size_t i = 0; i < columns.size(); ++i) {
        const auto &column = columns[i];
        query << column.name << " " << column.type;
        if (i < columns.size() - 1) {
            query << ", ";
        }
    }

    query << ")";

    return this->create_table(query.str());
}

bool DbConnector::delete_row(const std::string &tableName, const DbRow &data) {
    std::unique_lock lock(m_database_mutex);
    if (!is_open()) {
        eFatal("[DbConnector] Database not open");
    }

    if (data.size() == 0) {
        eWarning("[DbConnector] {}(false): [ImplementationInsert] DBRow is empty", file());
        return false;
    }

    std::string query = fmt::format("DELETE FROM {} WHERE ", tableName);
    std::string where;

    for (auto &el : data) {
        where += el.first + "= ? AND ";
    }

    where.erase(where.size() - 5, 5);
    query += where;

    sqlite3_stmt *stmt = nullptr;
    int           rc   = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        eWarning("[DbConnector] {}(false): {}", file(), query);
        eWarning("[DbConnector] Prepare failed: {}", sqlite3_errmsg(db));
        return false;
    }

    if (!implementation_prepare(tableName, data, stmt)) {
        eWarning("[DbConnector] Delete row. Bind failed: {}", sqlite3_errmsg(db));
        eWarning("{}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        eWarning("[DbConnector] DeleteRow.Execution failed: {}", sqlite3_errmsg(db));
        eWarning("[DbConnector] {}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        return false;
    }

#ifdef ENABLE_SQLITE_TRUE_LOGS
    eLog("[DbConnector] {}(true): {}", file(), query);
#endif
    sqlite3_finalize(stmt);
    return true;
}

bool DbConnector::table_exists(const std::string &table) {
    std::string query = fmt::format("SELECT name FROM sqlite_master WHERE type='table' AND name='{}';", table);
    return select(query).size() > 0;
}

bool DbConnector::drop_table(const std::string &table) {
    return query(fmt::format("DROP TABLE IF EXISTS {}", table));
}

std::uint64_t DbConnector::count(const std::string &table, const std::string &where) {
    std::string query = fmt::format("SELECT COUNT(*) FROM {}", table);
    if (!where.empty())
        query += fmt::format(" WHERE {}", where);

    // TODO: check if not error
    auto res = select(query);
    if (res.empty())
        return 0;
    return std::stoll(res[0]["COUNT(*)"]);
}

std::string DbConnector::file() const {
    const std::unique_lock lock(m_database_mutex);
    return m_file;
}

bool DbConnector::is_open() const {
    const std::unique_lock lock(m_database_mutex);
    return m_open;
}

std::vector<std::string> DbConnector::table_names() {
    std::vector<std::string> res;
    auto selectResult = select("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;");

    for (auto row : selectResult) {
        res.push_back(row["name"]);
    }

    return res;
}

std::vector<DBColumn> DbConnector::table_columns(const std::string &table) {
    if (table.empty()) {
        eFatal("[DbConnector] Try to get empty table columns");
    }

    auto sel = select("PRAGMA table_info('" + table + "')");
    if (sel.size() == 0)
        return {};

    std::vector<DBColumn> columns;
    for (auto &el : sel)
        columns.push_back(DBColumn { .name = el["name"], .type = el["type"] });

    return columns;
}

bool DbConnector::query(std::string query) {
    std::unique_lock lock(m_database_mutex);
    if (!is_open()) {
        eFatal("[DbConnector] Database not open");
    }

    sqlite3_stmt *stmt           = nullptr;
    const auto    prepare_result = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (prepare_result != SQLITE_OK) {
        eWarning("[DbConnector] Query prepare error: {}", sqlite3_errmsg(db));
        return false;
    }
    int res = sqlite3_step(stmt);

#ifndef ENABLE_SQLITE_TRUE_LOGS
    if (res != SQLITE_DONE)
#endif
        eWarning("[DbConnector] {}({}): {}", file(), (res == SQLITE_DONE ? "true" : "false"), query);
    if (res != SQLITE_DONE)
        eWarning("[DbConnector] Query error: {}", sqlite3_errmsg(db));

    sqlite3_finalize(stmt);
    return res == SQLITE_DONE;
}

boost::json::object DbConnector::to_json_object() {
    const std::unique_lock lock(m_database_mutex);
    if (!is_open()) {
        eFatal("[DbConnector] Database not open");
    }

    boost::json::object json;

    const auto tables = table_names();
    for (const auto &table : tables) {
        auto result = select_all(table);

        boost::json::array array;
        for (const auto &row : result) {
            boost::json::object object;
            for (const auto &[key, value] : row) {
                object[key] = value;
            }
            array.push_back(std::move(object));
        }

        json[table] = std::move(array);
    }

    return json;
}

std::string DbConnector::to_json() {
    return boost::json::serialize(to_json_object());
}

sqlite3 *DbConnector::getDb() const {
    const std::unique_lock lock(m_database_mutex);
    return db;
}

bool DbConnector::implementation_prepare(const std::string &tableName, const DbRow &data, sqlite3_stmt *stmt) {
    int  rc;
    auto columns  = table_columns(tableName);
    int  fieldNum = 1;

    for (auto &el : data) {
        std::string toFind = el.first;
        auto        it     = std::find_if(columns.begin(), columns.end(), [&toFind](const DBColumn &column) {
            return column.name == toFind;
        });
        if (it == columns.end()) {
            eWarning("[DbConnector] ImplementationPrepare: Column find error");
            return false;
        }

        int  indx   = std::distance(columns.begin(), it);
        auto column = columns[indx].type;

        if (column == "BLOB")
            rc = sqlite3_bind_blob(stmt, fieldNum, el.second.data(), int(el.second.size()), SQLITE_STATIC);
        else if (column == "TEXT" || column == "JSON")
            rc = sqlite3_bind_text(stmt, fieldNum, el.second.data(), int(el.second.size()), SQLITE_STATIC);
        else if (column == "INT")
            rc = sqlite3_bind_int(stmt, fieldNum, std::stoi(el.second));
        else if (column == "INTEGER")
            rc = sqlite3_bind_int64(stmt, fieldNum, std::stoll(el.second));
        else if (column == "REAL" || column == "NUMERIC")
            rc = sqlite3_bind_double(stmt, fieldNum, std::stod(el.second.data()));
        else {
            eWarning("[DbConnector] ImplementationPrepare: Column type not supported");
            return false;
        }
        fieldNum++;

        if (rc != SQLITE_OK) {
            return false;
        }
    }

    return true;
}

bool DbConnector::implementation_insert(const std::string &tableName, const DbRow &data, bool isReplace) {
    std::unique_lock lock(m_database_mutex);
    if (!is_open()) {
        eFatal("[DbConnector] Database not open");
    }

    if (data.size() == 0) {
        eWarning("[DbConnector] {}(false): [ImplementationInsert] DBRow is empty", file());
        return false;
    }

    std::string queryType = isReplace ? "REPLACE" : "IGNORE";
    std::string query     = fmt::format("INSERT OR {} INTO {} ", queryType, tableName);
    std::string fields;
    std::string values;

    for (auto &el : data) {
        fields += fmt::format("'{}', ", el.first);
        values += "?, ";
    }

    fields.erase(fields.size() - 2, 2);
    values.erase(values.size() - 2, 2);
    query += fmt::format("({}) VALUES ({})", fields, values);

    sqlite3_stmt *stmt = nullptr;
    int           rc   = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        eWarning("[DbConnector] {}(false): {}", file(), query);
        eWarning("[DbConnector] ImplementationInsert: prepare failed: {}", sqlite3_errmsg(db));
        return false;
    }

    if (!implementation_prepare(tableName, data, stmt)) {
        eWarning("[DbConnector] ImplementationInsert: Bind failed: {}", sqlite3_errmsg(db));
        eWarning("[DbConnector] {}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        return false;
    }

    // char *expanded = sqlite3_expanded_sql(stmt);
    // if (expanded) {
    //     eLog("DbConnector::implementation_insert, FULL QUERY:\n{}", expanded);
    //     sqlite3_free(expanded);
    // }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        eWarning("[DbConnector] ImplementationInsert: Execution failed: {}", sqlite3_errmsg(db));
        eWarning("[DbConnector] {}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        return false;
    }

    int changes = sqlite3_changes(db);
    if (changes == 0 && !isReplace) {
        // eWarning("[DbConnector] ImplementationInsert: No rows affected: {}", sqlite3_errmsg(db));
        // eWarning("[DbConnector] {}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        return false;
    }

#ifdef ENABLE_SQLITE_TRUE_LOGS
    eLog("[DbConnector] {}(true): {}", file(), query);
#endif
    sqlite3_finalize(stmt);
    return true;
}

std::pair<std::string, uint64_t> DbConnector::DbConnector::hash_size(const std::string &order_by) {
    const std::unique_lock lock(m_database_mutex);
    blake3_hasher          hasher;
    blake3_hasher_init(&hasher);
    uint64_t total = 0;

    for (const auto &table : table_names()) {
        blake3_hasher_update(&hasher, table.data(), table.size());
        total += table.size();

        for (const auto &col : table_columns(table)) {
            blake3_hasher_update(&hasher, col.name.data(), col.name.size());
            // blake3_hasher_update(&hasher, col.type.data(), col.type.size());
            total += col.name.size(); // + col.type.size();
        }

        sqlite3_stmt *stmt = nullptr;
        const auto    prepared =
            sqlite3_prepare_v2(db,
                               fmt::format("SELECT * FROM {} ORDER BY {}", table, order_by).c_str(),
                               -1,
                               &stmt,
                               nullptr);
        if (prepared != SQLITE_OK) {
            eWarning("[DbConnector] Hash prepare error: {}", sqlite3_errmsg(db));
            return { {}, 0 };
        }
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            for (int i = 0; i < sqlite3_column_count(stmt); i++) {
                auto bytes = sqlite3_column_bytes(stmt, i);
                blake3_hasher_update(&hasher, sqlite3_column_blob(stmt, i), bytes);
                total += bytes;
            }
        }
        sqlite3_finalize(stmt);
    }

    uint8_t hash[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, hash, BLAKE3_OUT_LEN);
    return { fmt::format("{:02x}", fmt::join(hash, hash + BLAKE3_OUT_LEN, "")), total };
}
