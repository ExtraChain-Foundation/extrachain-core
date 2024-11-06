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

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QDebug>
#include <QJsonDocument>
#include <QMutex>

#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <boost/core/demangle.hpp>

#include "extrachain_global.h"
#include "utils/exc_utils.h"
#include "utils/db_schema.h"

struct sqlite3;
struct sqlite3_stmt;

static QMutex dbmutex;

typedef std::unordered_map<std::string, std::string> DBRow;

namespace Utils {
template <typename T>
DBRow toDbRow(const T &obj) {
    auto  json = Json::serializeValue(obj);
    DBRow result;
    for (const auto &field : json.as_object()) {
        const auto &value = field.value();
        if (value.is_null()) {
            result[field.key()] = std::string();
        } else if (value.is_string()) {
            result[field.key()] = std::string(value.as_string());
        } else {
            result[field.key()] = boost::json::serialize(value);
        }
    }
    return result;
}

template <typename T>
std::expected<T, Utils::ParseError> fromDbRow(const DBRow &map) {
    try {
        using namespace boost::mp11;

        boost::json::object json;
        for (const auto &[key, value] : map) {
            mp_for_each<boost::describe::describe_members<T, boost::describe::mod_any_access>>([&](auto D) {
                if constexpr (!std::is_same_v<decltype(D), magic::custom_magic_tag>) {
                    if (key == magic::detail::clean_field_name(D.name)) {
                        using MemberType = std::remove_reference_t<decltype(std::declval<T>().*D.pointer)>;

                        if constexpr (magic::is_optional<MemberType>::value) {
                            if (value.empty()) {
                                json[key] = nullptr;
                            } else {
                                json[key] = stringToJsonValue(value, typeid(MemberType));
                            }
                        } else if constexpr (boost::describe::has_describe_members<MemberType>::value) {
                            json[key] = value;
                        } else {
                            json[key] = stringToJsonValue(value, typeid(MemberType));
                        }
                    }
                }
            });
        }

        return Json::deserialize<T>(boost::json::serialize(json)).transform_error([](const std::string &err) {
            qDebug() << "Json parse error:" << err;
            return Utils::ParseError::Invalid;
        });
    } catch (...) {
        return std::unexpected(Utils::ParseError::Invalid);
    }
}
} // namespace Utils

struct DBColumn {
    std::string name;
    std::string type;
    // bool notNull = false;
    // std::string defaultValue;
    // int primaryKey = -1;

    operator QString() const {
        return "DBColumn(name: " + QString::fromStdString(name) + ", type: " + QString::fromStdString(type)
               + ")";
    }
};

enum class DBConnectorType {
    Regular,
    Compressed
};
FORMAT_ENUM(DBConnectorType)

// TODO: while select, open check in query, std::vector<DBColumn>

class EXTRACHAIN_EXPORT DBConnector {
protected:
    std::string     m_file;
    bool            m_open = false;
    sqlite3        *db     = nullptr;
    DBConnectorType m_type = DBConnectorType::Regular;

public:
    explicit DBConnector(const std::string &filePath, DBConnectorType type = DBConnectorType::Regular);
    explicit DBConnector(
        const std::filesystem::path &filePath,
        DBConnectorType              type = DBConnectorType::Regular);
    explicit DBConnector(const char *filePath, DBConnectorType type = DBConnectorType::Regular);
    DBConnector(DBConnector &&db);
    ~DBConnector();

public:
    static QString sqlite_version();

    bool               open();
    bool               close();
    std::vector<DBRow> select(std::string query, std::string tableName = "", DBRow binds = {});
    std::vector<DBRow> selectAll(std::string table, int limit = -1);
    bool               insert(const std::string &tableName, const DBRow &data);
    bool               replace(const std::string &tableName, const DBRow &data);
    bool               update(const std::string &query);
    bool               createTable(const std::string &query);
    std::expected<std::string, SqlCreateError> createTable(const DbSchema &query);
    bool                                       deleteRow(const std::string &tableName, const DBRow &data);
    bool                                       deleteTable(const std::string &name);
    bool                                       tableExists(const std::string &table);
    bool                                       dropTable(const std::string &table);
    std::uint64_t                              count(const std::string &table, const std::string &where = "");
    std::string                                file() const;
    bool                                       isOpen() const;
    std::vector<std::string>                   tableNames();
    std::vector<DBColumn>                      tableColumns(const std::string &table);

public:
    bool          query(std::string query);
    QJsonObject   toJsonObject();
    QJsonDocument toJsonDocument();

public:
    sqlite3 *getDb() const;

private:
    bool implementationPrepare(const std::string &tableName, const DBRow &data, sqlite3_stmt *stmt);
    bool implementationInsert(const std::string &tableName, const DBRow &data, bool isReplace);
};
#endif // DB_CONNECTOR_H
