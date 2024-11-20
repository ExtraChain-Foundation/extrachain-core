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

#include <fmt/format.h>
#include <regex>
#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <memory>
#include <boost/algorithm/string/join.hpp>

#include "utils/exc_utils.h"

enum class SqlCreateError {
    EmptyIdentifier,
    InvalidIdentifierFormat,
    ReservedKeyword,
    InvalidValue,
    SqlInjectionRisk,
    AutoincrementNotInteger
};

enum class SqlAutoincrement {
    Yes,
    No
};

enum class SqlInclusiveness {
    Inclusive, // <= or >=
    Exclusive  // < or >
};

enum class ColumnType {
    Integer,
    Real,
    Text,
    Blob,
    Json
};

class SQLValidator {
public:
    static std::expected<void, SqlCreateError> validate_identifier(std::string_view identifier);
    static std::expected<void, SqlCreateError> validate_value(std::string_view value);
    static std::string                         escape_string(std::string_view value);

private:
    static const std::vector<std::string_view>& get_reserved_words();
    static const std::regex&                    get_identifier_regex();
    static const std::regex&                    get_value_regex();
};

template <typename T>
struct Bound {
    T                value;
    SqlInclusiveness inclusive;

    std::expected<std::string, SqlCreateError> to_sql(std::string_view column, bool is_upper) const {
        const char* op = is_upper ? (inclusive == SqlInclusiveness::Inclusive ? "<=" : "<")
                                  : (inclusive == SqlInclusiveness::Inclusive ? ">=" : ">");

        if constexpr (std::is_arithmetic_v<T>) {
            return fmt::format("{} {} {}", column, op, value);
        } else {
            auto validation = SQLValidator::validate_value(value);
            if (!validation) {
                return std::unexpected(validation.error());
            }
            return fmt::format("{} {} {}", column, op, SQLValidator::escape_string(value));
        }
    }
};

template <typename T>
struct Interval {
    Bound<T> lower;
    Bound<T> upper;

    std::expected<std::string, SqlCreateError> to_sql(std::string_view column) const {
        auto lower_sql = lower.to_sql(column, false);
        if (!lower_sql) {
            return std::unexpected(lower_sql.error());
        }

        auto upper_sql = upper.to_sql(column, true);
        if (!upper_sql) {
            return std::unexpected(upper_sql.error());
        }

        return fmt::format("({} AND {})", *lower_sql, *upper_sql);
    }
};

class DbColumn {
public:
    DbColumn()
        : m_type(ColumnType::Integer) {
    }
    explicit DbColumn(std::string_view name, ColumnType type);
    virtual ~DbColumn() = default;

    DbColumn& primary_key(SqlAutoincrement autoincrement = SqlAutoincrement::No);
    DbColumn& not_null();
    DbColumn& unique();
    DbColumn& default_value(std::string_view value);
    DbColumn& check(std::string_view condition);

    template <typename... Args>
    DbColumn& one_of(Args&&... values) {
        std::vector<std::string> vals;
        vals.reserve(sizeof...(Args));
        bool has_error = false;

        auto process_value = [&](auto&& value) {
            auto formatted = format_value(std::forward<decltype(value)>(value));
            if (!formatted) {
                m_validation_error = formatted.error();
                has_error          = true;
            } else {
                vals.push_back(*formatted);
            }
        };

        (process_value(std::forward<Args>(values)), ...);

        if (!has_error) {
            m_checks.push_back(fmt::format("CHECK ({} IN ({}))", m_name, boost::algorithm::join(vals, ", ")));
        }
        return *this;
    }

    template <typename U>
    DbColumn& between(
        const U&         lower,
        const U&         upper,
        SqlInclusiveness lower_inclusive = SqlInclusiveness::Inclusive,
        SqlInclusiveness upper_inclusive = SqlInclusiveness::Inclusive) {
        Interval<U> interval { Bound<U> { lower, lower_inclusive }, Bound<U> { upper, upper_inclusive } };

        if (auto sql = interval.to_sql(m_name); !sql) {
            m_validation_error = sql.error();
        } else {
            m_checks.push_back(fmt::format("CHECK {}", *sql));
        }
        return *this;
    }

    std::expected<std::string, SqlCreateError> to_sql() const;
    const std::optional<SqlCreateError>&       validation_error() const;

private:
    std::string_view get_type_name() const;

    template <typename U>
    static std::expected<std::string, SqlCreateError> format_value(const U& value) {
        if constexpr (std::is_arithmetic_v<U>) {
            return std::to_string(value);
        } else {
            if (auto validation = SQLValidator::validate_value(value); !validation) {
                return std::unexpected(validation.error());
            }
            return SQLValidator::escape_string(value);
        }
    }

    std::string                   m_name;
    ColumnType                    m_type;
    bool                          m_is_primary_key   = false;
    bool                          m_is_not_null      = false;
    bool                          m_is_unique        = false;
    bool                          m_is_autoincrement = false;
    std::string                   m_default_value;
    std::vector<std::string>      m_checks;
    std::optional<SqlCreateError> m_validation_error;

    BOOST_DESCRIBE_CLASS(
        DbColumn,
        (),
        (),
        (),
        (m_name,
         m_type,
         m_is_primary_key,
         m_is_not_null,
         m_is_unique,
         m_is_autoincrement,
         m_default_value,
         m_checks))
};

class DbSchema {
public:
    explicit DbSchema();
    explicit DbSchema(std::string_view table_name);

    DbSchema(DbSchema&& other) noexcept            = default;
    DbSchema& operator=(DbSchema&& other) noexcept = default;

    DbSchema& add_column(DbColumn&& column);

    template <typename... Cols>
    DbSchema& add_columns(Cols&&... cols) {
        m_columns.reserve(m_columns.size() + sizeof...(Cols));
        (add_column(std::move(cols)), ...);
        return *this;
    }

    std::expected<std::string, SqlCreateError> to_sql() const;
    std::string                                to_json() const;
    const std::optional<SqlCreateError>&       validation_error() const;

private:
    std::string                            m_table_name;
    std::vector<std::unique_ptr<DbColumn>> m_columns;
    std::optional<SqlCreateError>          m_validation_error;

    BOOST_DESCRIBE_CLASS(DbSchema, (), (), (), (m_table_name, m_columns))
};

namespace sqlite::literals {
DbColumn operator""_int(const char* name, size_t);
DbColumn operator""_real(const char* name, size_t);
DbColumn operator""_text(const char* name, size_t);
DbColumn operator""_blob(const char* name, size_t);
DbColumn operator""_json(const char* name, size_t);
} // namespace sqlite::literals
