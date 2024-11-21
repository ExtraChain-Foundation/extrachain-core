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

#pragma once

#include "utils/db_connector.h"
#include "sqlite3.h"
#include "boost/describe.hpp"

class DBConnectorStrict : public DbConnector {
public:
    explicit DBConnectorStrict(const std::string& filePath, DbConnectorType type = DbConnectorType::Regular)
        : DbConnector(filePath, type) {
    }
    DBConnectorStrict(DBConnectorStrict&& rhs)
        : DbConnector(std::move(rhs)) {
    }
    ~DBConnectorStrict() = default;

    DBConnectorStrict(const DBConnectorStrict&)            = delete;
    DBConnectorStrict& operator=(const DBConnectorStrict&) = delete;
    DBConnectorStrict& operator=(DBConnectorStrict&&)      = delete;

    template <typename T>
    bool createTableStrict(const std::string& tableName, bool ifNotExists = true) {
        if (!is_open()) {
            eFatal("[DbConnector] Database not open");
        }

        if constexpr (!boost::describe::has_describe_members<T>::value) {
            eLog("[DbConnector] CreateTableStrict: Type is not described with BOOST_DESCRIBE_STRUCT");
            return false;
        }

        std::string query =
            fmt::format("CREATE TABLE {}{} (", ifNotExists ? "IF NOT EXISTS " : "", tableName);

        bool isFirst = true;
        boost::mp11::mp_for_each<boost::describe::describe_members<
            T,
            boost::describe::mod_any_access | boost::describe::mod_inherited>>([&](auto D) {
            if (!isFirst) {
                query += ", ";
            }
            isFirst = false;

            std::string fieldName = magic::detail::clean_field_name(D.name);
            using FieldType = std::decay_t<decltype(magic::invoke_member(std::declval<T>(), D.pointer))>;

            std::string sqlType;
            if constexpr (std::is_same_v<FieldType, std::string>) {
                sqlType = "TEXT";
            } else if constexpr (
                std::is_same_v<FieldType, int> || std::is_same_v<FieldType, bool>
                || std::is_same_v<FieldType, char> || std::is_same_v<FieldType, short>
                || std::is_same_v<FieldType, unsigned short> || std::is_same_v<FieldType, unsigned int>) {
                sqlType = "INT";
            } else if constexpr (
                std::is_same_v<FieldType, long long> || std::is_same_v<FieldType, unsigned long long>) {
                sqlType = "INTEGER";
            } else if constexpr (std::is_floating_point_v<FieldType>) {
                sqlType = "REAL";
            } else if constexpr (
                magic::is_uint8_array<FieldType>::value || std::is_same_v<FieldType, std::vector<uint8_t>>) {
                sqlType = "BLOB";
            } else {
                sqlType = "TEXT";
            }

            query += fmt::format("'{}' {}", fieldName, sqlType);
        });

        query += ")";

        dbmutex.lock();
        char* errMsg = nullptr;
        int   rc     = sqlite3_exec(db, query.c_str(), nullptr, nullptr, &errMsg);

        if (rc != SQLITE_OK) {
            eLog("[DbConnector] CreateTableStrict failed: {}", (errMsg ? errMsg : "unknown error"));
            eLog("{} (false): {}", file().c_str(), query.c_str());
            if (errMsg) {
                sqlite3_free(errMsg);
            }
            dbmutex.unlock();
            return false;
        }

#ifdef ENABLE_SQLITE_TRUE_LOGS
        eLog("{} (true): {}", file().c_str(), query.c_str());
#endif

        dbmutex.unlock();
        return true;
    }

    template <typename T>
    bool insertStrict(const std::string& tableName, const T& data) {
        return this->implementationInsertStrict(tableName, data, false);
    }

    template <typename T>
    bool replaceStrict(const std::string& tableName, const T& data) {
        return this->implementationInsertStrict(tableName, data, true);
    }

private:
    template <typename T>
    bool implementationPrepareStrict(const std::string& tableName, const T& data, sqlite3_stmt* stmt) {
        if constexpr (!boost::describe::has_describe_members<T>::value) {
            return false;
        }

        auto columns  = table_columns(tableName);
        int  fieldNum = 1;
        bool success  = true;

        boost::mp11::mp_for_each<boost::describe::describe_members<
            T,
            boost::describe::mod_any_access | boost::describe::mod_inherited>>([&](auto D) {
            if (!success) {
                return;
            }

            std::string fieldName = magic::detail::clean_field_name(D.name);

            auto it = std::find_if(columns.begin(), columns.end(), [&fieldName](const DBColumn& column) {
                return column.name == fieldName;
            });

            if (it == columns.end()) {
                eLog("[DbConnector] ImplementationPrepareStrict: Column not found: {}", fieldName);
                success = false;
                return;
            }

            const auto& value = magic::invoke_member(data, D.pointer);
            eLog("{} {}", fieldName, value);
            int rc;

            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>) {
                eLog("{} {} {} {}", typeid(value).name(), value, value.size(), value.length());
                rc = sqlite3_bind_text(stmt, fieldNum, value.c_str(), int(value.length()), SQLITE_TRANSIENT);
            } else if constexpr (std::is_integral_v<std::decay_t<decltype(value)>>) {
                if (it->type == "INT") {
                    rc = sqlite3_bind_int(stmt, fieldNum, static_cast<int>(value));
                } else if (it->type == "INTEGER") {
                    rc = sqlite3_bind_int64(stmt, fieldNum, static_cast<sqlite3_int64>(value));
                } else {
                    success = false;
                    return;
                }
            } else if constexpr (std::is_floating_point_v<std::decay_t<decltype(value)>>) {
                if (it->type == "REAL" || it->type == "NUMERIC") {
                    rc = sqlite3_bind_double(stmt, fieldNum, static_cast<double>(value));
                } else {
                    success = false;
                    return;
                }
            } else if constexpr (
                magic::is_uint8_array<std::decay_t<decltype(value)>>::value
                || std::is_same_v<std::decay_t<decltype(value)>, std::vector<uint8_t>>) {
                if (it->type == "BLOB") {
                    rc = sqlite3_bind_blob(stmt, fieldNum, value.data(), int(value.size()), SQLITE_TRANSIENT);
                } else {
                    success = false;
                    return;
                }
            } else {
                std::string serialized = magic::magic(value);
                rc                     = sqlite3_bind_text(
                    stmt,
                    fieldNum,
                    serialized.c_str(),
                    int(serialized.size()),
                    SQLITE_STATIC);
            }

            if (rc != SQLITE_OK) {
                success = false;
                return;
            }

            fieldNum++;
        });

        if (!success) {
            sqlite3_finalize(stmt);
            return false;
        }

        return true;
    }

    template <typename T>
    bool implementationInsertStrict(const std::string& tableName, const T& data, bool isReplace = false) {
        if (!is_open()) {
            eFatal("[DbConnector] Database not open");
        }

        if constexpr (!boost::describe::has_describe_members<T>::value) {
            eLog("[DbConnector] InsertStrict: Type is not described with BOOST_DESCRIBE_STRUCT");
            return false;
        }

        std::vector<std::string> fields;
        boost::mp11::mp_for_each<boost::describe::describe_members<
            T,
            boost::describe::mod_any_access | boost::describe::mod_inherited>>([&fields](auto D) {
            fields.push_back(magic::detail::clean_field_name(D.name));
        });

        if (fields.empty()) {
            eLog("[DbConnector] {} (false): [ImplementationInsertStrict] No fields found", file());
            return false;
        }

        eLog("[DbConnector] Fields:");
        for (const auto& field : fields) {
            eLog("{}", field);
        }

        std::string queryType = isReplace ? "REPLACE" : "IGNORE";
        std::string query     = fmt::format("INSERT OR {} INTO {} ", queryType, tableName);

        std::string fieldsStr;
        std::string values;
        for (const auto& field : fields) {
            fieldsStr += fmt::format("{}, ", field);
            values += "?, ";
        }
        fieldsStr.erase(fieldsStr.size() - 2, 2);
        values.erase(values.size() - 2, 2);

        query += fmt::format("({}) VALUES ({})", fieldsStr, values);

        eLog("[DbConnector] Generated query: {}", query);

        dbmutex.lock();
        sqlite3_stmt* stmt = NULL;
        int           rc   = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
        dbmutex.unlock();

        if (rc != SQLITE_OK) {
            eLog("[DbConnector] Prepare error: {}", sqlite3_errmsg(db));
        }

        if (!implementationPrepareStrict(tableName, data, stmt)) {
            eLog("[DbConnector] ImplementationInsertStrict: Bind failed: {}", sqlite3_errmsg(db));
            eLog("{} (false): {}", file(), query);
            sqlite3_finalize(stmt);
            return false;
        }

        dbmutex.lock();
        if (rc != SQLITE_OK) {
            eLog().nospace() << file() << "(false):" << query;
            eLog("[DbConnector] ImplementationInsertStrict: prepare failed: {}", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            dbmutex.unlock();
            return false;
        }

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            eLog("[DbConnector] ImplementationInsertStrict: Execution failed: {}", sqlite3_errmsg(db));
            eLog("{} (false): {}", file(), query);
            sqlite3_finalize(stmt);
            dbmutex.unlock();
            return false;
        }

#ifdef ENABLE_SQLITE_TRUE_LOGS
        eLog("{} (true): {}", file(), query);
#endif

        sqlite3_finalize(stmt);
        dbmutex.unlock();
        return true;
    }
};
