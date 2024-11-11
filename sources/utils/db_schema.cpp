#include "utils/db_schema.h"

const std::vector<std::string_view>& SQLValidator::get_reserved_words() {
    static const std::vector<std::string_view> reserved_words = {
        "ADD",    "ALL",   "ALTER",    "AND",      "ANY",        "AS",     "ASC",      "BETWEEN",
        "BY",     "CASE",  "CHECK",    "COLUMN",   "CONSTRAINT", "CREATE", "DATABASE", "DEFAULT",
        "DELETE", "DESC",  "DISTINCT", "DROP",     "EXEC",       "EXISTS", "FOREIGN",  "FROM",
        "FULL",   "GROUP", "HAVING",   "IN",       "INDEX",      "INNER",  "INSERT",   "INTO",
        "IS",     "JOIN",  "KEY",      "LEFT",     "LIKE",       "LIMIT",  "NOT",      "NULL",
        "OR",     "ORDER", "OUTER",    "PRIMARY",  "PROCEDURE",  "RIGHT",  "ROWNUM",   "SELECT",
        "SET",    "TABLE", "TOP",      "TRUNCATE", "UNION",      "UNIQUE", "UPDATE",   "VALUES",
        "VIEW",   "WHERE"
    };
    return reserved_words;
}

const std::regex& SQLValidator::get_identifier_regex() {
    static const std::regex identifier_regex { R"(^[a-zA-Z_][a-zA-Z0-9_]*$)" };
    return identifier_regex;
}

const std::regex& SQLValidator::get_value_regex() {
    static const std::regex value_regex { R"(^[^'";]*$)" };
    return value_regex;
}

std::expected<void, SqlCreateError> SQLValidator::validate_identifier(std::string_view identifier) {
    if (identifier.empty()) {
        return std::unexpected(SqlCreateError::EmptyIdentifier);
    }

    if (!std::regex_match(identifier.begin(), identifier.end(), get_identifier_regex())) {
        return std::unexpected(SqlCreateError::InvalidIdentifierFormat);
    }

    auto upper_identifier = std::string(identifier);
    std::transform(upper_identifier.begin(), upper_identifier.end(), upper_identifier.begin(), ::toupper);

    for (auto& word : get_reserved_words()) {
        if (upper_identifier == word) {
            return std::unexpected(SqlCreateError::ReservedKeyword);
        }
    }

    return {};
}

std::expected<void, SqlCreateError> SQLValidator::validate_value(std::string_view value) {
    if (!std::regex_match(value.begin(), value.end(), get_value_regex())) {
        return std::unexpected(SqlCreateError::SqlInjectionRisk);
    }
    return {};
}

std::string SQLValidator::escape_string(std::string_view value) {
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('\'');

    for (char c : value) {
        if (c == '\'') {
            result.append("''");
        } else {
            result.push_back(c);
        }
    }

    result.push_back('\'');
    return result;
}

// DbColumn implementation
DbColumn::DbColumn(std::string_view name, ColumnType type)
    : m_type(type) {
    if (auto validation = SQLValidator::validate_identifier(name); !validation) {
        m_validation_error = validation.error();
    }
    m_name = std::string(name);
}

DbColumn& DbColumn::primary_key(SqlAutoincrement autoincrement) {
    m_is_primary_key = true;
    if (autoincrement == SqlAutoincrement::Yes) {
        if (m_type != ColumnType::Integer) {
            m_validation_error = SqlCreateError::AutoincrementNotInteger;
        } else {
            m_is_autoincrement = true;
        }
    }
    return *this;
}

DbColumn& DbColumn::not_null() {
    m_is_not_null = true;
    return *this;
}

DbColumn& DbColumn::unique() {
    m_is_unique = true;
    return *this;
}

DbColumn& DbColumn::default_value(std::string_view value) {
    if (auto validation = SQLValidator::validate_value(value); !validation) {
        m_validation_error = validation.error();
    } else {
        m_default_value = fmt::format("DEFAULT {}", value);
    }
    return *this;
}

DbColumn& DbColumn::check(std::string_view condition) {
    if (auto validation = SQLValidator::validate_value(condition); !validation) {
        m_validation_error = validation.error();
    } else {
        m_checks.push_back(fmt::format("CHECK ({})", condition));
    }
    return *this;
}

std::string_view DbColumn::get_type_name() const {
    switch (m_type) {
    case ColumnType::Integer:
        return "INTEGER";
    case ColumnType::Real:
        return "REAL";
    case ColumnType::Text:
        return "TEXT";
    case ColumnType::Blob:
        return "BLOB";
    case ColumnType::Json:
        return "JSON";
    }
    return ""; // Should never happen
}

std::expected<std::string, SqlCreateError> DbColumn::to_sql() const {
    if (m_validation_error) {
        return std::unexpected(*m_validation_error);
    }

    std::string sql = fmt::format("{} {}", m_name, get_type_name());

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

const std::optional<SqlCreateError>& DbColumn::validation_error() const {
    return m_validation_error;
}

// DbSchema implementation
DbSchema::DbSchema() = default;

DbSchema::DbSchema(std::string_view table_name) {
    if (auto validation = SQLValidator::validate_identifier(table_name); !validation) {
        m_validation_error = validation.error();
    }
    m_table_name = std::string(table_name);
}

DbSchema& DbSchema::add_column(DbColumn&& column) {
    if (column.validation_error()) {
        m_validation_error = column.validation_error();
    }
    m_columns.push_back(std::make_unique<DbColumn>(std::move(column)));
    return *this;
}

std::expected<std::string, SqlCreateError> DbSchema::to_sql() const {
    if (m_validation_error) {
        return std::unexpected(*m_validation_error);
    }

    std::string sql = fmt::format("CREATE TABLE IF NOT EXISTS {} (\n", m_table_name);

    for (size_t i = 0; i < m_columns.size(); ++i) {
        auto column_sql = m_columns[i]->to_sql();
        if (!column_sql) {
            return std::unexpected(column_sql.error());
        }

        sql += "    " + *column_sql;
        if (i < m_columns.size() - 1) {
            sql += ",";
        }
        sql += "\n";
    }

    sql += ");";
    return sql;
}

std::string DbSchema::to_json() const {
    auto json = Json::serialize(*this);
    return json;
}

const std::optional<SqlCreateError>& DbSchema::validation_error() const {
    return m_validation_error;
}

// SQLite literals implementation
namespace sqlite::literals {

DbColumn operator""_int(const char* name, size_t) {
    return DbColumn(name, ColumnType::Integer);
}

DbColumn operator""_real(const char* name, size_t) {
    return DbColumn(name, ColumnType::Real);
}

DbColumn operator""_text(const char* name, size_t) {
    return DbColumn(name, ColumnType::Text);
}

DbColumn operator""_blob(const char* name, size_t) {
    return DbColumn(name, ColumnType::Blob);
}

DbColumn operator""_json(const char* name, size_t) {
    return DbColumn(name, ColumnType::Json);
}

} // namespace sqlite::literals
