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

#include "utils/db_connector.h"

#include <QDir>
#include <iostream>

// #define ENABLE_SQLITE_TRUE_LOGS

DBConnector::DBConnector()
{
    db = nullptr;
}

DBConnector::DBConnector(const std::string &name)
{
    this->open(name);
}

DBConnector::~DBConnector()
{
    // TODO: check if sqlite pointer is active
    if (db != nullptr)
    {
        close();
        //        sqlite3_db_release_memory(db);
    }
    // close();
}

bool DBConnector::open(const std::string &name)
{
    int rc = sqlite3_open(name.c_str(), &db);
    if (rc)
    {
        qDebug() << file().c_str() << " | failed to open DB:" << sqlite3_errmsg(db);
        return false;
    }
    else
    {
        m_file = name;
        m_open = true;
        return true;
    }
}

bool DBConnector::close()
{
    if (!m_open)
        return true;

    int rc = sqlite3_close_v2(db);
    if (rc)
    {
        qDebug() << sqlite3_errmsg(db);
        return false;
    }
    else
    {
        m_open = false;
        return true;
    }
}

std::vector<DBRow> DBConnector::select(std::string query, std::string tableName, DBRow binds)
{ // std::pair with status?
    dbmutex.lock();
    sqlite3_stmt *stmt;
    std::vector<DBRow> res;
    sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);

    if (!binds.empty())
    {
        dbmutex.unlock();
        if (!implementationPrepare(tableName, binds, stmt))
        {
            qDebug() << "select bind error";
            return {};
        }
        dbmutex.lock();
    }

    int rs = sqlite3_step(stmt);

    while (rs != SQLITE_DONE)
    {
        if (stmt == nullptr)
        {
            break;
        }

        DBRow row;
        int colNum = sqlite3_column_count(stmt);

        for (int i = 0; i < colNum; i++)
        {
            std::string n = sqlite3_column_name(stmt, i);
            std::string t;
            switch (sqlite3_column_type(stmt, i))
            {
            case SQLITE_BLOB: {
                int size = sqlite3_column_bytes(stmt, i);
                t = std::string(reinterpret_cast<const char *>(sqlite3_column_blob(stmt, i)), size);
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
            default:
                break;
            }

            row.insert({ n, t });
        }

        res.push_back(row);

        rs = sqlite3_step(stmt);
    }

    dbmutex.unlock();
#ifndef ENABLE_SQLITE_TRUE_LOGS
    if (rs != SQLITE_DONE)
#endif
        if (QString(query.c_str()).indexOf("SELECT  type") == -1)
            qDebug().nospace() << file().c_str() << "(" << (rs == SQLITE_DONE ? "true" : "false")
                               << "): " << query.c_str();
    if (rs != SQLITE_DONE)
    {
        qDebug() << file().c_str() << "error: " << sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        return {};
    }

    sqlite3_finalize(stmt);
    return res;
}

std::vector<DBRow> DBConnector::selectAll(std::string table, int limit)
{
    std::string query = "SELECT * FROM " + table + (limit > 0 ? " LIMIT " + std::to_string(limit) : "");
    return select(query);
}

bool DBConnector::insert(const std::string &tableName, const DBRow &data)
{
    return this->implementationInsert(tableName, data, false);
}

bool DBConnector::replace(const std::string &tableName, const DBRow &data)
{
    return this->implementationInsert(tableName, data, true);
}

bool DBConnector::update(const std::string &query)
{
    return this->query(query);
}

bool DBConnector::createTable(const std::string &query)
{
    QString queryTemp = QString::fromStdString(query).replace(QRegExp("\\s+"), " "); // temp
    return this->query(queryTemp.toStdString());
}

bool DBConnector::deleteRow(const std::string &tableName, const DBRow &data)
{
    if (data.size() == 0)
    {
        qDebug() << file().c_str() << "(false): [ImplementationInsert] DBRow is empty";
        return false;
    }

    std::string query = "DELETE FROM " + tableName + " WHERE ";
    std::string where;

    for (auto &el : data)
    {
        where += el.first + "= ? AND ";
    }

    where.erase(where.size() - 5, 5);
    query += where;

    dbmutex.lock();
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, NULL);

    dbmutex.unlock();
    if (!implementationPrepare(tableName, data, stmt))
    {
        qDebug() << "[DeleteRow] Bind failed:" << sqlite3_errmsg(db);
        qDebug() << file().c_str() << "(false):" << query.c_str();
        sqlite3_finalize(stmt);
        return false;
    }

    dbmutex.lock();
    if (rc != SQLITE_OK)
    {
        qDebug().nospace() << file().c_str() << "(false):" << query.c_str();
        qDebug() << "[DeleteRow] prepare failed:" << sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        dbmutex.unlock();
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        qDebug() << "[DeleteRow] Execution failed: " << sqlite3_errmsg(db);
        qDebug() << file().c_str() << "(false):" << query.c_str();
        sqlite3_finalize(stmt);
        dbmutex.unlock();
        return false;
    }

#ifdef ENABLE_SQLITE_TRUE_LOGS
    qDebug() << file().c_str() << "(true):" << query.c_str();
#endif
    sqlite3_finalize(stmt);
    dbmutex.unlock();
    return true;
}

bool DBConnector::deleteTable(const std::string &name)
{
    std::string query = "DROP TABLE " + name + ";";
    return this->query(query);
}

bool DBConnector::tableExists(const std::string &table)
{
    std::string query = "SELECT name FROM sqlite_master WHERE type='table' AND name='" + table + "';";
    return select(query).size() > 0;
}

bool DBConnector::dropTable(const std::string &table)
{
    return query("DROP TABLE IF EXISTS " + table);
}

int DBConnector::count(const std::string &table, const std::string &where)
{
    std::string query = "SELECT COUNT(*) FROM " + table;
    if (!where.empty())
        query += " WHERE " + where;

    auto res = select(query);
    if (res.empty())
        return 0;
    return std::stoi(res[0]["COUNT(*)"]);
}

std::string DBConnector::file() const
{
    // QString dbFile = sqlite3_db_filename(db, nullptr);
    // dbFile = dbFile.remove(0, QDir::currentPath().length());
    return m_file;
}

bool DBConnector::isOpen() const
{
    return m_open;
}

std::vector<std::string> DBConnector::tableNames()
{
    std::vector<std::string> res;
    auto selectResult = select("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;");

    for (auto row : selectResult)
    {
        res.push_back(row["name"]);
    }

    return res;
}

std::vector<DBColumn> DBConnector::tableColumns(const std::string &table)
{
    auto sel = select("PRAGMA table_info('" + table + "')");
    if (sel.size() == 0)
        return {};

    std::vector<DBColumn> columns;
    for (auto &el : sel)
        columns.push_back(DBColumn { .name = el["name"], .type = el["type"] });

    return columns;
}

bool DBConnector::query(std::string query)
{
    dbmutex.lock();
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    int res = sqlite3_step(stmt);

#ifndef ENABLE_SQLITE_TRUE_LOGS
    if (res != SQLITE_DONE)
#endif
        qDebug().nospace() << file().c_str() << "(" << (res == SQLITE_DONE ? "true" : "false")
                           << "): " << query.c_str();
    if (res != SQLITE_DONE)
        qDebug() << "Query error: " << sqlite3_errmsg(db);

    sqlite3_finalize(stmt);
    dbmutex.unlock();
    return res == SQLITE_DONE;
}

sqlite3 *DBConnector::getDb() const
{
    return db;
}

bool DBConnector::implementationPrepare(const std::string &tableName, const DBRow &data, sqlite3_stmt *stmt)
{
    int rc;
    auto columns = tableColumns(tableName);
    int fieldNum = 1;

    for (auto &el : data)
    {
        std::string toFind = el.first;
        auto it = std::find_if(columns.begin(), columns.end(),
                               [&toFind](const DBColumn &column) { return column.name == toFind; });
        if (it == columns.end())
        {
            qDebug() << "[ImplementationPrepare] Column find error";
            sqlite3_finalize(stmt);
            return false;
        }

        int indx = std::distance(columns.begin(), it);
        auto column = columns[indx].type;
        // qDebug() << "[ImplementationPrepare] Finded" << column.c_str();

        if (column == "BLOB")
            rc = sqlite3_bind_blob(stmt, fieldNum, el.second.data(), el.second.size(), SQLITE_STATIC);
        else if (column == "TEXT")
            rc = sqlite3_bind_text(stmt, fieldNum, el.second.data(), el.second.size(), SQLITE_STATIC);
        else if (column == "INT")
            rc = sqlite3_bind_int(stmt, fieldNum, std::stoi(el.second));
        else if (column == "INTEGER")
            rc = sqlite3_bind_int64(stmt, fieldNum, std::stoll(el.second));
        else if (column == "REAL" || column == "NUMERIC")
            rc = sqlite3_bind_double(stmt, fieldNum, std::stod(el.second.data()));
        else
        {
            qDebug() << "[ImplementationPrepare] Column type not supported";
            sqlite3_finalize(stmt);
            return false;
        }
        fieldNum++;

        if (rc != SQLITE_OK)
        {
            sqlite3_finalize(stmt);
            return false;
        }
    }

    return true;
}

bool DBConnector::implementationInsert(const std::string &tableName, const DBRow &data, bool isReplace)
{
    if (data.size() == 0)
    {
        qDebug() << file().c_str() << "(false): [ImplementationInsert] DBRow is empty";
        return false;
    }

    std::string queryType = isReplace ? "REPLACE" : "IGNORE";
    std::string query = "INSERT OR " + queryType + " INTO " + tableName + " ";
    std::string fields;
    std::string values;

    for (auto &el : data)
    {
        fields += "'" + el.first + "', ";
        values += "?, ";
    }

    fields.erase(fields.size() - 2, 2);
    values.erase(values.size() - 2, 2);
    query += "(" + fields + ") VALUES (" + values + ")";

    dbmutex.lock();
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, NULL);

    dbmutex.unlock();
    if (!implementationPrepare(tableName, data, stmt))
    {
        qDebug() << "[ImplementationInsert] Bind failed:" << sqlite3_errmsg(db);
        qDebug() << file().c_str() << "(false):" << query.c_str();
        sqlite3_finalize(stmt);
        return false;
    }

    dbmutex.lock();
    if (rc != SQLITE_OK)
    {
        qDebug().nospace() << file().c_str() << "(false):" << query.c_str();
        qDebug() << "[ImplementationInsert] prepare failed:" << sqlite3_errmsg(db);
        sqlite3_finalize(stmt);
        dbmutex.unlock();
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE)
    {
        qDebug() << "[ImplementationInsert] Execution failed: " << sqlite3_errmsg(db);
        qDebug() << file().c_str() << "(false):" << query.c_str();
        sqlite3_finalize(stmt);
        dbmutex.unlock();
        return false;
    }

#ifdef ENABLE_SQLITE_TRUE_LOGS
    qDebug() << file().c_str() << "(true):" << query.c_str();
#endif
    sqlite3_finalize(stmt);
    dbmutex.unlock();
    return true;
}
