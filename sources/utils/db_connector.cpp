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

std::vector<DBRow> DBConnector::select(std::string query) // std::pair with status
{
    dbmutex.lock();
    sqlite3_stmt *stmt;
    std::vector<DBRow> res;
    sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
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
            case (SQLITE_BLOB): {
                int size = sqlite3_column_bytes(stmt, i);
                t = std::string(reinterpret_cast<const char *>(sqlite3_column_blob(stmt, i)), size);
                break;
            }
            case (SQLITE3_TEXT):
                t = (reinterpret_cast<const char *>(sqlite3_column_text(stmt, i)));
                break;
            case (SQLITE_INTEGER):
                t = std::to_string(sqlite3_column_int64(stmt, i));
                break;
            case (SQLITE_FLOAT):
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
    if (data.size() == 0)
    {
        qDebug() << file().c_str() << "(false): Insert: DBRow is empty";
        return false;
    }
    std::string query = prepareInsert(tableName, data, false);
    return this->query(query);
}

bool DBConnector::replace(const std::string &tableName, const DBRow &data)
{
    if (data.size() == 0)
    {
        qDebug() << file().c_str() << "(false): Replace: DBRow is empty";
        return false;
    }

    std::string query = prepareInsert(tableName, data, true);
    return this->query(query);
}

std::string DBConnector::prepareInsert(const std::string &tableName, const DBRow &data, bool isReplace)
{
    std::string type = isReplace ? "REPLACE" : "IGNORE";
    std::string query = "INSERT OR " + type + " INTO ";
    query.append(tableName + " (");
    std::string f;
    std::string v;
    // for(auto [column, value] : data)
    for (auto it = data.cbegin(); it != data.cend(); ++it)
    {
        std::string s = it->first;
        s.insert(0, "'");
        s.append("', ");
        f.append(s);

        if (it->first == "message")
            s = "?, ";
        else
        {
            s = it->second; // ReplaceAll(it->second, "'", "''");
            if (it->second == "auto_max")
            {
                s = "(SELECT IFNULL(MAX(" + it->first + "), 0) + 1 FROM " + tableName + "), ";
            }
            else
            {
                s.insert(0, "'");
                s.append("', ");
            }
        }
        v.append(s);
    }
    f.erase(f.size() - 2, 2);
    v.erase(v.size() - 2, 2);
    query.append(f);
    query.append(" ) VALUES (");
    query.append(v);
    // if (!noEnd)
    query.append(" );");
    return query;
}

bool DBConnector::update(const std::string &query)
{
    return this->query(query);
}

bool DBConnector::createTable(const std::string &query)
{
    return this->query(query);
}

bool DBConnector::deleteRow(const std::string &tableName, const std::string &nameColumn,
                            const std::string &query)
{
    std::string _query = "DELETE FROM " + tableName;
    _query.append(" WHERE " + nameColumn + " = '" + query + "'");
    return this->query(_query);
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

bool DBConnector::insertWithData(const std::string &query, const QByteArray &data)
{
    int rc;
    sqlite3_stmt *stmt = NULL;
    dbmutex.lock();
    rc = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        qDebug().nospace() << file().c_str() << "(false):" << query.c_str();
        qDebug() << "prepare failed:" << sqlite3_errmsg(db);
    }
    else
    {
        // SQLITE_STATIC because the statement is finalized
        // before the buffer is freed:
        rc = sqlite3_bind_blob(stmt, 1, data.data(), data.size(), SQLITE_STATIC);
        if (rc != SQLITE_OK)
        {
            qDebug() << "bind failed:" << sqlite3_errmsg(db);
            qDebug() << file().c_str() << "(false):" << query.c_str();
            dbmutex.unlock();
            return false;
        }
        else
        {
            rc = sqlite3_step(stmt);
            if (rc != SQLITE_DONE)
            {
                qDebug() << "execution failed: " << sqlite3_errmsg(db);
                qDebug() << file().c_str() << "(false):" << query.c_str();
                dbmutex.unlock();
                return false;
            }
        }
    }

    qDebug() << file().c_str() << "(true):" << query.c_str();
    sqlite3_finalize(stmt);
    dbmutex.unlock();
    return true;
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
