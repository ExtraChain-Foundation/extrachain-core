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

#include "utils/db_iterator.h"

#include "sqlite3.h"
#include "utils/exc_logs.h"

DbIterator::DbIterator(sqlite3_stmt *stmt)
    : m_stmt(stmt)
    , m_done(false) {
}

bool DbIterator::next() {
    if (!m_stmt)
        return false;
    if (m_done)
        return false;

    int rs = sqlite3_step(m_stmt);
    if (rs == SQLITE_ROW)
        return true;
    m_done   = true;
    m_failed = rs != SQLITE_DONE;
    if (m_failed)
        eWarning("[DbIterator] Read failed: {}", sqlite3_errstr(rs));
    return false;
}

DbIterator::~DbIterator() {
    if (m_stmt) {
        sqlite3_finalize(m_stmt);
    }
}

std::string DbIterator::getString(int column) {
    const auto text = reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, column));
    return text == nullptr ? std::string { } : std::string(text, sqlite3_column_bytes(m_stmt, column));
}

int64_t DbIterator::getInt64(int column) {
    return sqlite3_column_int64(m_stmt, column);
}

double DbIterator::getDouble(int column) {
    return sqlite3_column_double(m_stmt, column);
}

std::string DbIterator::getBlob(int column) {
    const int size = sqlite3_column_bytes(m_stmt, column);
    return size == 0 ? std::string { }
                     : std::string(reinterpret_cast<const char*>(sqlite3_column_blob(m_stmt, column)), size);
}

int DbIterator::columnCount() {
    return sqlite3_column_count(m_stmt);
}

std::string DbIterator::columnName(int column) {
    return sqlite3_column_name(m_stmt, column);
}

DbColumnType DbIterator::columnType(int column) {
    return static_cast<DbColumnType>(sqlite3_column_type(m_stmt, column));
}

std::optional<std::string> DbIterator::read_value(sqlite3_stmt* statement, int column) {
    switch (sqlite3_column_type(statement, column)) {
    case SQLITE_NULL:
        return std::nullopt;
    case SQLITE_INTEGER:
        return std::to_string(sqlite3_column_int64(statement, column));
    case SQLITE_FLOAT:
        return fmt::format("{}", sqlite3_column_double(statement, column));
    case SQLITE_BLOB: {
        const auto size = sqlite3_column_bytes(statement, column);
        return size == 0
                   ? std::string { }
                   : std::string(reinterpret_cast<const char*>(sqlite3_column_blob(statement, column)), size);
    }
    default: {
        const auto* text = sqlite3_column_text(statement, column);
        return text == nullptr
                   ? std::string { }
                   : std::string(reinterpret_cast<const char*>(text), sqlite3_column_bytes(statement, column));
    }
    }
}

std::string DbIterator::getValue(int column) {
    return read_value(m_stmt, column).value_or("");
}

std::unordered_map<std::string, std::string> DbIterator::dbRow() {
    std::unordered_map<std::string, std::string> row;

    for (int i = 0; i < columnCount(); i++) {
        if (columnType(i) == DbColumnType::Null)
            continue;
        std::string name  = columnName(i);
        std::string value = getValue(i);

        row.insert({ name, value });
    }

    return row;
}
