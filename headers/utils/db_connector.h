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

// TODO: while select, open check in query, normal insert, std::vector<DBColumn>

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
    std::vector<DBRow> select(std::string query);
    std::vector<DBRow> selectAll(std::string table, int limit = -1);
    bool insert(const std::string &tableName, const DBRow &data);
    bool replace(const std::string &tableName, const DBRow &data);
    std::string prepareInsert(const std::string &tableName, const DBRow &data, bool isReplace);
    bool update(const std::string &query);
    bool createTable(const std::string &query);
    bool deleteRow(const std::string &tableName, const std::string &nameColumn, const std::string &query);
    bool deleteTable(const std::string &name);
    bool tableExists(const std::string &table);
    bool dropTable(const std::string &table);
    int count(const std::string &table, const std::string &where = "");
    bool insertWithData(const std::string &query, const QByteArray &data);
    std::string file() const;
    bool isOpen() const;
    std::vector<std::string> tableNames();

public:
    bool query(std::string query);

public:
    sqlite3 *getDb() const;
};
#endif // DB_CONNECTOR_H
