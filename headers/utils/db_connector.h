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

#ifndef DB_CONNECTOR_H
#define DB_CONNECTOR_H
#include <QByteArray>
#include <QDebug>

#include <vector>
#include <unordered_map>
#include <utility>
#include <algorithm>

#include <QMutex>

#include "sqlite3.h"
static QMutex dbmutex;
typedef std::unordered_map<std::string, std::string> DBRow;

struct DBColumn
{
    std::string name;
    std::string type;
    // bool notNull = false;
    // std::string defaultValue;
    // int primaryKey = -1;

    operator QString() const
    {
        return "DBColumn(name: " + QString::fromStdString(name) + ", type: " + QString::fromStdString(type)
            + ")";
    }
};

// TODO: while select, open check in query, std::vector<DBColumn>

class DBConnector
{
private:
    std::string m_file;
    bool m_open = false;
    sqlite3 *db;

public:
    DBConnector();
    DBConnector(const std::string &name);
    ~DBConnector();

public:
    bool open(const std::string &name);
    bool close();
    std::vector<DBRow> select(std::string query, std::string tableName = "", DBRow binds = {});
    std::vector<DBRow> selectAll(std::string table, int limit = -1);
    bool insert(const std::string &tableName, const DBRow &data);
    bool replace(const std::string &tableName, const DBRow &data);
    bool update(const std::string &query);
    bool createTable(const std::string &query);
    bool deleteRow(const std::string &tableName, const DBRow &data);
    bool deleteTable(const std::string &name);
    bool tableExists(const std::string &table);
    bool dropTable(const std::string &table);
    int count(const std::string &table, const std::string &where = "");
    std::string file() const;
    bool isOpen() const;
    std::vector<std::string> tableNames();
    std::vector<DBColumn> tableColumns(const std::string &table);

public:
    bool query(std::string query);

public:
    sqlite3 *getDb() const;

private:
    bool implementationPrepare(const std::string &tableName, const DBRow &data, sqlite3_stmt *stmt);
    bool implementationInsert(const std::string &tableName, const DBRow &data, bool isReplace);
};
#endif // DB_CONNECTOR_H
