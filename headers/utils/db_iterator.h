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

#ifndef DB_ITERATOR_H
#define DB_ITERATOR_H

enum class DbColumnType {
    Integer = 1,
    Float = 2,
    Text = 3,
    Blob = 4,
    Null = 5
};

struct sqlite3_stmt;

class DbIterator {
public:
    DbIterator(sqlite3_stmt* stmt);
    ~DbIterator();

    bool next();

    std::string getString(int column);
    int64_t getInt64(int column);
    double getDouble(int column);
    std::string getBlob(int column);

    int columnCount();
    std::string columnName(int column);
    DbColumnType columnType(int column);

    std::string getValue(int column);
    std::unordered_map<std::string, std::string> dbRow();

private:
    sqlite3_stmt* m_stmt;
    bool m_done;
};
#endif // DB_ITERATOR_H
