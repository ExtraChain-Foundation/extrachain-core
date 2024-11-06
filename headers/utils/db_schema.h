#pragma once

#include <fmt/format.h>
#include <regex>
#include <string>
#include <string_view>
#include <vector>
#include <expected>
#include <boost/algorithm/string/join.hpp>

enum class SqlCreateError {
    EmptyIdentifier,
    InvalidIdentifierFormat,
    ReservedKeyword,
    InvalidValue,
    SqlInjectionRisk
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

namespace sqlite {
struct Integer {
    using cpp_type                         = int64_t;
    static constexpr std::string_view name = "INTEGER";
};

struct Real {
    using cpp_type                         = double;
    static constexpr std::string_view name = "REAL";
};

struct Text {
    using cpp_type                         = std::string;
    static constexpr std::string_view name = "TEXT";
};

struct Blob {
    using cpp_type                         = std::vector<uint8_t>;
    static constexpr std::string_view name = "BLOB";
};
}

template <typename T>
struct Bound {
    T    value;
    bool inclusive;

    std::expected<std::string, SqlCreateError> to_sql(std::string_view column) const {
        if constexpr (std::is_arithmetic_v<T>) {
            return fmt::format("{} {} {}", column, inclusive ? ">=" : ">", value);
        } else {
            auto validation = SQLValidator::validate_value(value);
            if (!validation) {
                return std::unexpected(validation.error());
            }
            return fmt::format(
                "{} {} {}",
                column,
                inclusive ? ">=" : ">",
                SQLValidator::escape_string(value));
        }
    }
};

template <typename T>
struct Interval {
    Bound<T> lower;
    Bound<T> upper;

    std::expected<std::string, SqlCreateError> to_sql(std::string_view column) const {
        auto lower_sql = lower.to_sql(column);
        if (!lower_sql) {
            return std::unexpected(lower_sql.error());
        }

        auto upper_sql = upper.to_sql(column);
        if (!upper_sql) {
            return std::unexpected(upper_sql.error());
        }

        return fmt::format("({} AND {})", *lower_sql, *upper_sql);
    }
};

template <typename T>
class Column {
public:
    explicit Column(std::string_view name) {
        if (auto validation = SQLValidator::validate_identifier(name); !validation) {
            m_validation_error = validation.error();
        }
        m_name = std::string(name);
    }

    Column(Column&& other) noexcept            = default;
    Column& operator=(Column&& other) noexcept = default;

    Column& primary_key(bool autoincrement = false) {
        m_is_primary_key   = true;
        m_is_autoincrement = autoincrement;
        return *this;
    }

    Column& not_null() {
        m_is_not_null = true;
        return *this;
    }

    Column& unique() {
        m_is_unique = true;
        return *this;
    }

    Column& default_value(std::string_view value) {
        if (auto validation = SQLValidator::validate_value(value); !validation) {
            m_validation_error = validation.error();
        } else {
            m_default_value = fmt::format("DEFAULT {}", value);
        }
        return *this;
    }

    Column& check(std::string_view condition) {
        if (auto validation = SQLValidator::validate_value(condition); !validation) {
            m_validation_error = validation.error();
        } else {
            m_checks.push_back(fmt::format("CHECK ({})", condition));
        }
        return *this;
    }

    template <typename... Args>
    Column& one_of(Args&&... values) {
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
    Column&
    between(const U& lower, const U& upper, bool lower_inclusive = true, bool upper_inclusive = true) {
        Interval<U> interval { Bound<U> { lower, lower_inclusive }, Bound<U> { upper, upper_inclusive } };

        if (auto sql = interval.to_sql(m_name); !sql) {
            m_validation_error = sql.error();
        } else {
            m_checks.push_back(fmt::format("CHECK {}", *sql));
        }
        return *this;
    }

    std::expected<std::string, SqlCreateError> to_sql() const {
        if (m_validation_error) {
            return std::unexpected(*m_validation_error);
        }

        std::string sql = fmt::format("{} {}", m_name, T::name);

        if (m_is_primary_key) {
            sql += " PRIMARY KEY";
            if (m_is_autoincrement) {
                sql += " AUTOINCREMENT";
            }
        }
        if (m_is_not_null)
            sql += " NOT NULL";
        if (m_is_unique)
            sql += " UNIQUE";
        if (!m_default_value.empty())
            sql += " " + m_default_value;
        for (const auto& check : m_checks) {
            sql += " " + check;
        }

        return sql;
    }

private:
    std::string                   m_name;
    bool                          m_is_primary_key   = false;
    bool                          m_is_not_null      = false;
    bool                          m_is_unique        = false;
    bool                          m_is_autoincrement = false;
    std::string                   m_default_value;
    std::vector<std::string>      m_checks;
    std::optional<SqlCreateError> m_validation_error;

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
};

class DbSchema {
public:
    explicit DbSchema(std::string_view table_name) {
        if (auto validation = SQLValidator::validate_identifier(table_name); !validation) {
            m_validation_error = validation.error();
        }
        m_table_name = std::string(table_name);
    }

    DbSchema(DbSchema&& other) noexcept            = default;
    DbSchema& operator=(DbSchema&& other) noexcept = default;

    template <typename T>
    DbSchema& add_column(Column<T>&& column) {
        if (auto sql = column.to_sql(); !sql) {
            m_validation_error = sql.error();
        } else {
            m_columns.push_back(*sql);
        }
        return *this;
    }

    template <typename... Cols>
    DbSchema& add_columns(Cols&&... cols) {
        m_columns.reserve(m_columns.size() + sizeof...(Cols));
        (add_column(std::move(cols)), ...);
        return *this;
    }

    std::expected<std::string, SqlCreateError> to_sql() const;

private:
    std::string                   m_table_name;
    std::vector<std::string>      m_columns;
    std::vector<std::string>      m_constraints;
    std::optional<SqlCreateError> m_validation_error;
};

namespace sqlite::literals {
Column<sqlite::Integer> operator""_int(const char* name, size_t);
Column<sqlite::Real>    operator""_real(const char* name, size_t);
Column<sqlite::Text>    operator""_text(const char* name, size_t);
Column<sqlite::Blob>    operator""_blob(const char* name, size_t);
}
