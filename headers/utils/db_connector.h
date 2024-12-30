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

#include <algorithm>
#include <unordered_map>
#include <utility>
#include <vector>

#include <QByteArray>
#include <QJsonDocument>
#include <QMutex>

#include <boost/describe.hpp>
#include <boost/mp11.hpp>
#include <boost/core/demangle.hpp>

#include "extrachain_global.h"
#include "utils/exc_utils.h"
#include "utils/db_schema.h"
#include "utils/db_iterator.h"

struct sqlite3;
struct sqlite3_stmt;

static QMutex dbmutex;

using DbRow      = std::unordered_map<std::string, std::string>;
using DbRowBytes = std::unordered_map<std::string, std::vector<std::uint8_t>>;

namespace Utils {
    template <typename T>
    DbRow to_dbrow(const T &obj) {
        auto  json = Json::serialize_value(obj);
        DbRow result;

        boost::mp11::mp_for_each<
            boost::describe::describe_members<T,
                                              boost::describe::mod_any_access | boost::describe::mod_inherited>>(
            [&](auto D) {
                if constexpr (!std::is_same_v<decltype(D), magic::custom_magic_tag>) {
                    const auto &field_value = obj.*D.pointer;
                    const auto  field_name  = magic::detail::clean_field_name(D.name);

                    using FieldType = std::remove_reference_t<decltype(field_value)>;
                    if constexpr (magic::is_optional<FieldType>::value) {
                        if (!field_value.has_value()) {
                            result[field_name] = "";
                        }
                    }
                }
            });

        for (const auto &field : json.as_object()) {
            const auto &value = field.value();
            if (value.is_string()) {
                result[field.key()] = std::string(value.as_string());
            } else if (value.is_null()) {
                result[field.key()] = "";
            } else {
                result[field.key()] = boost::json::serialize(value);
            }
        }

        return result;
    }

    template <typename T>
    std::expected<T, Utils::ParseError> from_dbrow(const DbRow &map) {
        try {
            boost::json::object json;
            for (const auto &[key, value] : map) {
                boost::mp11::mp_for_each<boost::describe::describe_members<T,
                                                                           boost::describe::mod_any_access
                                                                               | boost::describe::mod_inherited>>(
                    [&](auto D) {
                        if constexpr (!std::is_same_v<decltype(D), magic::custom_magic_tag>) {
                            if (key == magic::detail::clean_field_name(D.name)) {
                                using MemberType = std::remove_reference_t<decltype(std::declval<T>().*D.pointer)>;

                                if constexpr (magic::is_optional<MemberType>::value) {
                                    if (value.empty()) {
                                        json[key] = boost::json::value(nullptr);
                                    } else {
                                        using BaseType = typename MemberType::value_type;
                                        json[key]      = stringToJsonValue(value, typeid(BaseType));
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
                eLog("Json parse error: {}", err);
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
        return "DBColumn(name: " + QString::fromStdString(name) + ", type: " + QString::fromStdString(type) + ")";
    }
};

enum class DbConnectorType {
    Regular,
    Compressed
};
// FORMAT_ENUM(DbConnectorType)

// TODO: while select, open check in query, std::vector<DBColumn>

class EXTRACHAIN_EXPORT DbConnector {
protected:
    std::string     m_file;
    bool            m_open = false;
    sqlite3        *db     = nullptr;
    DbConnectorType m_type = DbConnectorType::Regular;

public:
    explicit DbConnector(const std::string &filePath, DbConnectorType type = DbConnectorType::Regular);
    explicit DbConnector(const std::filesystem::path &filePath, DbConnectorType type = DbConnectorType::Regular);
    explicit DbConnector(const FsPath &filePath, DbConnectorType type = DbConnectorType::Regular);
    explicit DbConnector(const char *filePath, DbConnectorType type = DbConnectorType::Regular);
    DbConnector(DbConnector &&db);
    ~DbConnector();

public:
    static QString sqlite_version();

    bool                        open();
    bool                        close();
    std::vector<DbRow>          select(std::string query, std::string tableName = "", DbRow binds = {});
    std::vector<DbRow>          select_all(std::string table, int limit = -1);
    std::unique_ptr<DbIterator> select_while(std::string query, std::string table_name, DbRow binds = {});
    bool                        insert(const std::string &tableName, const DbRow &data);
    bool                        replace(const std::string &tableName, const DbRow &data);
    bool                        update(const std::string &query);
    bool update(const std::string &table_name, const DbRow &set_data, const DbRow &where_data);
    bool create_table(const std::string &query);
    std::expected<std::string, SqlCreateError> create_table(const DbSchema &query);
    bool                                       delete_row(const std::string &tableName, const DbRow &data);
    bool                                       table_exists(const std::string &table);
    bool                                       drop_table(const std::string &table);
    std::uint64_t                              count(const std::string &table, const std::string &where = "");
    std::string                                file() const;
    bool                                       is_open() const;
    std::vector<std::string>                   table_names();
    std::vector<DBColumn>                      table_columns(const std::string &table);

    std::pair<std::string, uint64_t> hash_size(const std::string &order_by = "rowid");

public:
    bool          query(std::string query);
    QJsonObject   toJsonObject();
    QJsonDocument toJsonDocument();

public:
    sqlite3 *getDb() const;

private:
    bool implementation_prepare(const std::string &tableName, const DbRow &data, sqlite3_stmt *stmt);
    bool implementation_insert(const std::string &tableName, const DbRow &data, bool isReplace);

    BOOST_DESCRIBE_CLASS(DbConnector, (), (), (), (m_file, m_open, m_type))
};
