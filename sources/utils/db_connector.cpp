#include "headers/utils/db_connector.h"

#include <iostream>

DBConnector::DBConnector()
{
    db = nullptr;
}

DBConnector::DBConnector(std::string name)
{
    this->open(name);
}

DBConnector::~DBConnector()
{
    // TODO: check if sqlite pointer is active
    // close();
}

bool DBConnector::open(std::string name)
{
    int rc = sqlite3_open(name.c_str(), &db);
    if (rc)
    {
        qDebug() << "Failed to open DB:" << sqlite3_errmsg(db);
        return false;
    }
    else
        return true;
}

bool DBConnector::close()
{
    int rc = sqlite3_close_v2(db);
    if (rc)
    {
        qDebug() << sqlite3_errmsg(db);
        return false;
    }
    else
        return true;
}

std::vector<DBRow> DBConnector::select(std::string query)
{
    sqlite3_stmt *stmt;
    std::vector<DBRow> res;
    sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    int rs = sqlite3_step(stmt);

    while (true)
    {
        if (stmt == nullptr)
        {
            // std::cout << "Query(false): " << query << std::endl;
            // std::cout << "Query error: " << sqlite3_errmsg(db) << std::endl;
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
            case (SQLITE3_TEXT):
                t = (reinterpret_cast<const char *>(sqlite3_column_text(stmt, i)));
                break;
            case (SQLITE_INTEGER):
                t = std::to_string(sqlite3_column_int(stmt, i));
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
        if (rs == SQLITE_DONE)
            break;
    }

    std::cout << "Query(" << (rs == SQLITE_DONE ? "true" : "false") << "): " << query << std::endl;
    if (rs != SQLITE_DONE)
    {
        std::cout << "Query error: " << sqlite3_errmsg(db) << std::endl;
        return {};
    }

    sqlite3_finalize(stmt);
    return res;
}

bool DBConnector::insert(std::string tableName, DBRow data)
{
    std::string query = "INSERT OR IGNORE INTO ";
    query.append(tableName + " (");
    std::string f;
    std::string v;
    // for(auto [column, value] : data)
    for (DBRow::iterator it = data.begin(); it != data.end(); ++it)
    {
        std::string s = it->first;
        s.insert(0, "'");
        s.append("', ");
        f.append(s);

        s = it->second;
        s.insert(0, "'");
        s.append("', ");
        v.append(s);
    }
    f.erase(f.size() - 2, 2);
    v.erase(v.size() - 2, 2);
    query.append(f);
    query.append(" ) VALUES (");
    query.append(v);
    query.append(" );");
    // qDebug() << query.c_str();
    return this->query(query);
}

bool DBConnector::update(std::string query)
{
    return this->query(query);
}

bool DBConnector::createTable(std::string query)
{
    return this->query(query);
}

bool DBConnector::deleteRow(std::string query)
{
    return this->query(query);
}

bool DBConnector::deleteTable(std::string name)
{
    std::string query = "DROP TABLE " + name + ";";
    return this->query(query);
}

bool DBConnector::tableExists(std::string table)
{
    std::string query = "SELECT name FROM sqlite_master WHERE type='table' AND name='" + table + "';";
    return select(query).size() > 0;
}

bool DBConnector::dropTable(std::string table)
{
    return query("DROP TABLE IF EXISTS " + table);
}

bool DBConnector::query(std::string query)
{
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    int res = sqlite3_step(stmt);

    std::cout << "Query(" << (res == SQLITE_DONE ? "true" : "false") << "): " << query << std::endl;
    if (res != SQLITE_DONE)
        std::cout << "Query error: " << sqlite3_errmsg(db) << std::endl;

    sqlite3_finalize(stmt);
    return res == SQLITE_DONE;
}

sqlite3 *DBConnector::getDb() const
{
    return db;
}
