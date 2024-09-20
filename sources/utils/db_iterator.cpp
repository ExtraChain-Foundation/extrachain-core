/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

DBIterator::DBIterator(sqlite3_stmt *stmt)
    : m_stmt(stmt)
    , m_done(false) {
}

bool DBIterator::next() {
    if (!m_stmt)
        return false;
    if (m_done)
        return false;

    int rs = sqlite3_step(m_stmt);
    if (rs == SQLITE_DONE) {
        m_done = true;
        return false;
    }
    return true;
}

DBIterator::~DBIterator() {
    if (m_stmt) {
        sqlite3_finalize(m_stmt);
    }
}

std::string DBIterator::getString(int column) {
    return reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, column));
}

int64_t DBIterator::getInt64(int column) {
    return sqlite3_column_int64(m_stmt, column);
}

double DBIterator::getDouble(int column) {
    return sqlite3_column_double(m_stmt, column);
}

std::string DBIterator::getBlob(int column) {
    int size = sqlite3_column_bytes(m_stmt, column);
    std::string str = std::string(reinterpret_cast<const char *>(sqlite3_column_blob(m_stmt, column)), size);
    return str;
}

int DBIterator::columnCount() {
    return sqlite3_column_count(m_stmt);
}

std::string DBIterator::columnName(int column) {
    return sqlite3_column_name(m_stmt, column);
}

DbColumnType DBIterator::columnType(int column) {
    return static_cast<DbColumnType>(sqlite3_column_type(m_stmt, column));
}

std::string DBIterator::getValue(int column) {
    std::string value;

    switch (columnType(column)) {
    case DbColumnType::Integer:
        value = std::to_string(sqlite3_column_int64(m_stmt, column));
        break;
    case DbColumnType::Float:
        value = std::to_string(sqlite3_column_double(m_stmt, column));
        break;
    case DbColumnType::Text:
        value = (reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, column)));
        break;
    case DbColumnType::Blob: {
        int size = sqlite3_column_bytes(m_stmt, column);
        value = std::string(reinterpret_cast<const char *>(sqlite3_column_blob(m_stmt, column)), size);
        break;
    }
    case DbColumnType::Null:
        qFatal("TODO: test");
        break;
    }

    return value;
}

std::unordered_map<std::string, std::string> DBIterator::dbRow() {
    std::unordered_map<std::string, std::string> row;

    for (int i = 0; i < columnCount(); i++) {
        std::string name = columnName(i);
        std::string value = getValue(i);

        row.insert({ name, value });
    }

    return row;
}
