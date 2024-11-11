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

#include "sqlite3.h"
#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>

#include "utils/exc_logs.h"

// #define ENABLE_SQLITE_TRUE_LOGS

DbConnector::DbConnector(const std::string &filePath, DBConnectorType type) {
    if (filePath.empty()) {
        eFatal("[DBConnector] Empty file name");
    }

    if (type == DBConnectorType::Compressed) {
        m_type       = type;
        this->m_file = filePath + ".temp"; // TODO: + random str?

        if (!QFile::exists(filePath.c_str()))
            return;
        QFile file(filePath.c_str());
        if (!file.open(QFile::ReadOnly)) {
            eFatal("[DBConnector] Can't open db file");
        }
        auto  data_uncompressed = qUncompress(file.readAll());
        QFile fileTemp(QString::fromStdString(filePath) + ".temp");
        fileTemp.open(QFile::WriteOnly);
        fileTemp.write(data_uncompressed);
        fileTemp.close();
        return;
    }

    this->m_file = filePath;
}

DbConnector::DbConnector(const std::filesystem::path &filePath, DBConnectorType type)
    : DbConnector(filePath.string(), type) {
}

DbConnector::DbConnector(const char *filePath, DBConnectorType type)
    : DbConnector(std::string(filePath), type) {
}

DbConnector::DbConnector(DbConnector &&rhs) {
    if (this == &rhs)
        return;

    this->m_file = std::move(rhs.m_file);
    this->m_open = rhs.m_open;
    this->db     = rhs.db;
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

QString DbConnector::sqlite_version() {
    return sqlite3_libversion();
}

bool DbConnector::open() {
    if (isOpen()) {
        eFatal("[DBConnector] Double open");
        return false;
    }
    if (m_file.empty()) {
        eFatal("[DBConnector] File name empty");
        return false;
    }

    int rc = sqlite3_open(m_file.c_str(), &db);
    if (rc) {
        eLog("[DBConnector] {} | failed to open DB: {}", m_file, sqlite3_errmsg(db));
        return false;
    } else {
        m_open = true;

        if (!QFile::exists(m_file.c_str()))
            eFatal("db open error: {}", m_file);

        return true;
    }
}

bool DbConnector::close() {
    if (!m_open)
        return true;

    int rc = sqlite3_close_v2(db);
    if (rc) {
        eLog("[DBConnector] {}", sqlite3_errmsg(db));
        return false;
    } else {
        m_open = false;

        if (m_type == DBConnectorType::Compressed) {
            QFile file(m_file.c_str());
            if (!file.open(QFile::ReadOnly)) {
                eFatal("Can't open db file");
            }
            auto  data_compressed = qCompress(file.readAll());
            QFile fileTemp(QString::fromStdString(m_file).mid(0, m_file.size() - 4));
            fileTemp.open(QFile::WriteOnly);
            fileTemp.write(data_compressed);
            fileTemp.close();
            file.remove();
        }

        return true;
    }
}

std::vector<DbRow> DbConnector::select(std::string query, std::string tableName, DbRow binds) {
    // std::pair with status?
    if (!isOpen()) {
        eFatal("[DBConnector] Database not open");
    }

    dbmutex.lock();
    sqlite3_stmt      *stmt;
    std::vector<DbRow> res;
    sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);

    if (!binds.empty()) {
        dbmutex.unlock();
        if (!implementationPrepare(tableName, binds, stmt)) {
            eLog("[DBConnector] Select bind error");
            return {};
        }
        dbmutex.lock();
    }

    int rs = sqlite3_step(stmt);

    while (rs != SQLITE_DONE) {
        if (stmt == nullptr) {
            break;
        }

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
            default:
                break;
            }

            row.insert({ n, t });
        }

        res.push_back(row);

        rs = sqlite3_step(stmt);
    }

    dbmutex.unlock();

    if (rs != SQLITE_DONE) {
        eLog("[DBConnector] {} {} error: {}", m_file, m_type, sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return {};
    }

    sqlite3_finalize(stmt);
    return res;
}

std::vector<DbRow> DbConnector::selectAll(std::string table, int limit) {
    std::string query =
        fmt::format("SELECT * FROM {}{}", table, limit > 0 ? " LIMIT " + std::to_string(limit) : "");
    return select(query);
}

bool DbConnector::insert(const std::string &tableName, const DbRow &data) {
    return this->implementationInsert(tableName, data, false);
}

bool DbConnector::replace(const std::string &tableName, const DbRow &data) {
    return this->implementationInsert(tableName, data, true);
}

bool DbConnector::update(const std::string &query) {
    return this->query(query);
}

bool DbConnector::createTable(const std::string &query) {
    QString queryTemp = QString::fromStdString(query).replace(QRegularExpression("\\s+"), " ");
    return this->query(queryTemp.toStdString());
}

std::expected<std::string, SqlCreateError> DbConnector::createTable(const DbSchema &query) {
    auto sql = query.to_sql();
    if (!sql.has_value())
        return sql;

    createTable(sql.value());
    return sql;
}

bool DbConnector::deleteRow(const std::string &tableName, const DbRow &data) {
    if (!isOpen()) {
        eFatal("[DBConnector] Database not open");
    }

    if (data.size() == 0) {
        eLog("[DBConnector] {}(false): [ImplementationInsert] DBRow is empty", file());
        return false;
    }

    std::string query = fmt::format("DELETE FROM {} WHERE ", tableName);
    std::string where;

    for (auto &el : data) {
        where += el.first + "= ? AND ";
    }

    where.erase(where.size() - 5, 5);
    query += where;

    dbmutex.lock();
    sqlite3_stmt *stmt = NULL;
    int           rc   = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, NULL);

    dbmutex.unlock();
    if (!implementationPrepare(tableName, data, stmt)) {
        eLog("[DBConnector] Delete row. Bind failed: {}", sqlite3_errmsg(db));
        eLog("{}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        return false;
    }

    dbmutex.lock();
    if (rc != SQLITE_OK) {
        eLog("[DBConnector] {}(false): {}", file(), query);
        eLog("[DBConnector] Prepare failed: {}", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        dbmutex.unlock();
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        eLog("[DBConnector] DeleteRow.Execution failed: {}", sqlite3_errmsg(db));
        eLog("[DBConnector] {}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        dbmutex.unlock();
        return false;
    }

#ifdef ENABLE_SQLITE_TRUE_LOGS
    eLog("[DBConnector] {}(true): {}", file(), query);
#endif
    sqlite3_finalize(stmt);
    dbmutex.unlock();
    return true;
}

bool DbConnector::deleteTable(const std::string &name) {
    std::string query = fmt::format("DROP TABLE {};", name);
    return this->query(query);
}

bool DbConnector::tableExists(const std::string &table) {
    std::string query =
        fmt::format("SELECT name FROM sqlite_master WHERE type='table' AND name='{}';", table);
    return select(query).size() > 0;
}

bool DbConnector::dropTable(const std::string &table) {
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
    // QString dbFile = sqlite3_db_filename(db, nullptr);
    // dbFile = dbFile.remove(0, QDir::currentPath().length());
    return m_file;
}

bool DbConnector::isOpen() const {
    return m_open;
}

std::vector<std::string> DbConnector::tableNames() {
    std::vector<std::string> res;
    auto selectResult = select("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;");

    for (auto row : selectResult) {
        res.push_back(row["name"]);
    }

    return res;
}

std::vector<DBColumn> DbConnector::tableColumns(const std::string &table) {
    auto sel = select("PRAGMA table_info('" + table + "')");
    if (sel.size() == 0)
        return {};

    std::vector<DBColumn> columns;
    for (auto &el : sel)
        columns.push_back(DBColumn { .name = el["name"], .type = el["type"] });

    return columns;
}

bool DbConnector::query(std::string query) {
    if (!isOpen()) {
        eFatal("[DBConnector] Database not open");
    }

    dbmutex.lock();
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, nullptr);
    int res = sqlite3_step(stmt);

#ifndef ENABLE_SQLITE_TRUE_LOGS
    if (res != SQLITE_DONE)
#endif
        eLog("[DBConnector] {}({}): {}", file(), (res == SQLITE_DONE ? "true" : "false"), query);
    if (res != SQLITE_DONE)
        eLog("[DBConnector] Query error: {}", sqlite3_errmsg(db));

    sqlite3_finalize(stmt);
    dbmutex.unlock();
    return res == SQLITE_DONE;
}

QJsonObject DbConnector::toJsonObject() {
    if (!isOpen()) {
        qFatal("[DBConnector] Database not open");
    }

    QJsonObject json;

    const auto tables = tableNames();
    for (const auto &table : tables) {
        auto result = selectAll(table);

        QJsonArray array;
        for (const auto &row : result) {
            QJsonObject obj;
            for (const auto &[key, value] : row) {
                obj[key.c_str()] = value.c_str();
            }
            array << obj;
        }

        json[table.c_str()] = array;
    }

    return json;
}

QJsonDocument DbConnector::toJsonDocument() {
    auto object = toJsonObject();
    auto json   = QJsonDocument(std::move(object));
    return json;
}

sqlite3 *DbConnector::getDb() const {
    return db;
}

bool DbConnector::implementationPrepare(const std::string &tableName, const DbRow &data, sqlite3_stmt *stmt) {
    int  rc;
    auto columns  = tableColumns(tableName);
    int  fieldNum = 1;

    for (auto &el : data) {
        std::string toFind = el.first;
        auto        it     = std::find_if(columns.begin(), columns.end(), [&toFind](const DBColumn &column) {
            return column.name == toFind;
        });
        if (it == columns.end()) {
            eLog("[DBConnector] ImplementationPrepare: Column find error");
            sqlite3_finalize(stmt);
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
            eLog("[DBConnector] ImplementationPrepare: Column type not supported");
            sqlite3_finalize(stmt);
            return false;
        }
        fieldNum++;

        if (rc != SQLITE_OK) {
            sqlite3_finalize(stmt);
            return false;
        }
    }

    return true;
}

bool DbConnector::implementationInsert(const std::string &tableName, const DbRow &data, bool isReplace) {
    if (!isOpen()) {
        eFatal("[DBConnector] Database not open");
    }

    if (data.size() == 0) {
        eLog("[DBConnector] {}(false): [ImplementationInsert] DBRow is empty", file());
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

    dbmutex.lock();
    sqlite3_stmt *stmt = NULL;
    int           rc   = sqlite3_prepare_v2(db, query.c_str(), -1, &stmt, NULL);

    dbmutex.unlock();
    if (!implementationPrepare(tableName, data, stmt)) {
        eLog("[DBConnector] ImplementationInsert: Bind failed: {}", sqlite3_errmsg(db));
        eLog("[DBConnector] {}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        return false;
    }

    dbmutex.lock();
    if (rc != SQLITE_OK) {
        eLog("[DBConnector] {}(false): {}", file(), query);
        eLog("[DBConnector] ImplementationInsert: prepare failed: {}", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        dbmutex.unlock();
        return false;
    }

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        eLog("[DBConnector] ImplementationInsert: Execution failed: {}", sqlite3_errmsg(db));
        eLog("[DBConnector] {}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        dbmutex.unlock();
        return false;
    }

    int changes = sqlite3_changes(db);
    if (changes == 0 && !isReplace) {
        eLog("[DBConnector] ImplementationInsert: No rows affected: {}", sqlite3_errmsg(db));
        eLog("[DBConnector] {}(false): {}", file(), query);
        sqlite3_finalize(stmt);
        dbmutex.unlock();
        return false;
    }

#ifdef ENABLE_SQLITE_TRUE_LOGS
    eLog("[DBConnector] {}(true): {}", file(), query);
#endif
    sqlite3_finalize(stmt);
    dbmutex.unlock();
    return true;
}
