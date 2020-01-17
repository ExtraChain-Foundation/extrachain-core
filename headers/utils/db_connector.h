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

class DBConnector
{
private:
    std::string m_file;
    bool m_open = false;
    sqlite3 *db;

public:
    DBConnector();
    DBConnector(std::string name);
    ~DBConnector();

public:
    bool open(std::string name);
    bool close();
    std::vector<DBRow> select(std::string query);
    bool insert(std::string tableName, DBRow data);
    std::string prepareInsert(std::string tableName, DBRow data, bool noEnd = false);
    bool update(std::string query);
    bool createTable(std::string query);
    bool deleteRow(std::string tableName, std::string nameColumn, std::string query);
    bool deleteTable(std::string name);
    bool tableExists(std::string table);
    bool dropTable(std::string table);
    bool insertWithData(std::string query, QByteArray data);
    std::string file() const;
    bool isOpen() const;

public:
    bool query(std::string query);

public:
    sqlite3 *getDb() const;
};
#endif // DB_CONNECTOR_H

//    QElapsedTimer timer;
//    timer.start();
//    DBConnector dbc;
//    if (dbc.open("test-db"))
//    {
//        dbc.createTable("CREATE TABLE DFS (ID TEXT PRIMARY KEY NOT NULL, PATH TEXT NOT NULL)");

//        DBRow row1 = { { "ID", "0" }, { "PATH", "/data/images/0" } };
//        dbc.insert("DFS", row1);
//        for (int i = 0; i != 5000; ++i)
//        {
//            std::vector<DBRow> res2 = dbc.select("SELECT * FROM DFS");
//        }
//        dbc.deleteRow("DELETE from DFS WHERE ID='0';");
//        std::vector<DBRow> res1 = dbc.select("SELECT * FROM DFS");
//        //        qDebug() << "EEEEEEEEEEEEEEEEEEE";
//    }
//    else
//        qDebug() << "bI.";
//    qDebug() << timer.elapsed();
//    return 0;
