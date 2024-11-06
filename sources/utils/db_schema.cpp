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

std::expected<std::string, SqlCreateError> DbSchema::to_sql() const {
    if (m_validation_error) {
        return std::unexpected(*m_validation_error);
    }

    std::string sql = fmt::format("CREATE TABLE IF NOT EXISTS {} (\n", m_table_name);

    for (size_t i = 0; i < m_columns.size(); ++i) {
        sql += "    " + m_columns[i];
        if (i < m_columns.size() - 1 || !m_constraints.empty()) {
            sql += ",";
        }
        sql += "\n";
    }

    for (size_t i = 0; i < m_constraints.size(); ++i) {
        sql += "    " + m_constraints[i];
        if (i < m_constraints.size() - 1) {
            sql += ",";
        }
        sql += "\n";
    }

    sql += ");";
    return sql;
}

namespace sqlite::literals {
Column<sqlite::Integer> operator""_int(const char* name, size_t) {
    return Column<sqlite::Integer>(name);
}

Column<sqlite::Real> operator""_real(const char* name, size_t) {
    return Column<sqlite::Real>(name);
}

Column<sqlite::Text> operator""_text(const char* name, size_t) {
    return Column<sqlite::Text>(name);
}

Column<sqlite::Blob> operator""_blob(const char* name, size_t) {
    return Column<sqlite::Blob>(name);
}
}
