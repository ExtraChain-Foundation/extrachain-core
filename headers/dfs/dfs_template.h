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

#include <string>
#include <variant>
#include <vector>
#include <expected>
#include <chrono>
#include <optional>
#include <fmt/format.h>
#include <boost/algorithm/string/join.hpp>

#include "utils/db_schema.h"

class DfsTemplate {
public:
    enum class Type {
        Id,
        String,
        Email,
        Url,
        Username,
        Ip,
        Ipv4,
        Ipv6,
        Base64,
        Hex,
        Integer,
        Float,
        BigInt,
        BigFloat,
        Timestamp,
        Json,
        Phone
    };

    struct BaseSettings {
        std::optional<bool> required {};
        std::optional<bool> unique {};
    };

    struct StringSettings : BaseSettings {
        std::optional<size_t>                   min_length {};
        std::optional<size_t>                   max_length {};
        std::optional<std::string>              pattern {};
        std::optional<std::string>              default_value {};
        std::optional<std::vector<std::string>> allowed_values {};
    };

    struct NumberSettings : BaseSettings {
        std::optional<std::string>              min {};
        std::optional<std::string>              max {};
        std::optional<std::string>              default_value {};
        std::optional<std::vector<std::string>> allowed_values {};
    };

    struct JsonSettings : BaseSettings {
        std::optional<size_t> max_depth {};
    };

    struct TimestampSettings : BaseSettings {
        std::optional<bool> default_now {};
    };

    using FieldSettings =
        std::variant<BaseSettings, StringSettings, NumberSettings, JsonSettings, TimestampSettings>;

    struct Field {
        std::string   name;
        Type          type;
        FieldSettings settings;

        Field(std::string n, Type t, FieldSettings s)
            : name(std::move(n))
            , type(t)
            , settings(std::move(s)) {
        }
    };

    DfsTemplate(std::string name, std::initializer_list<Field> fields)
        : m_name(std::move(name))
        , m_fields(fields) {
    }

    std::expected<DbSchema, SqlCreateError> to_db_schema() const {
        DbSchema schema(m_name);

        for (const auto& field : m_fields) {
            auto column = std::visit(
                [&](const auto& settings) -> std::expected<DbColumn, SqlCreateError> {
                    DbColumn col(field.name, get_column_type(field.type));

                    if (field.type == Type::Id) {
                        col.primary_key(SqlAutoincrement::Yes);
                        return col;
                    }

                    if (settings.required && *settings.required) {
                        col.not_null();
                    }
                    if (settings.unique && *settings.unique) {
                        col.unique();
                    }

                    using S = std::decay_t<decltype(settings)>;
                    if constexpr (std::is_same_v<S, StringSettings>) {
                        if (settings.min_length || settings.max_length) {
                            col.check(
                                fmt::format("length({}) BETWEEN {} AND {}",
                                            field.name,
                                            settings.min_length.value_or(0),
                                            settings.max_length.value_or(std::numeric_limits<size_t>::max())));
                        }

                        if (settings.default_value) {
                            col.default_value(*settings.default_value);
                        }

                        if (settings.allowed_values && !settings.allowed_values->empty()) {
                            std::vector<std::string> quoted;
                            quoted.reserve(settings.allowed_values->size());
                            for (const auto& val : *settings.allowed_values) {
                                quoted.push_back(fmt::format("'{}'", val));
                            }
                            col.check(fmt::format("{} IN ({})", field.name, boost::algorithm::join(quoted, ", ")));
                        }
                    } else if constexpr (std::is_same_v<S, NumberSettings>) {
                        std::vector<std::string> conditions;

                        if (settings.min) {
                            conditions.push_back(fmt::format("{} >= {}", field.name, *settings.min));
                        }
                        if (settings.max) {
                            conditions.push_back(fmt::format("{} <= {}", field.name, *settings.max));
                        }

                        if (!conditions.empty()) {
                            col.check(boost::algorithm::join(conditions, " AND "));
                        }

                        if (settings.default_value) {
                            col.default_value(*settings.default_value);
                        }

                        if (settings.allowed_values && !settings.allowed_values->empty()) {
                            col.check(fmt::format("{} IN ({})",
                                                  field.name,
                                                  boost::algorithm::join(*settings.allowed_values, ", ")));
                        }
                    } else if constexpr (std::is_same_v<S, JsonSettings>) {
                        if (settings.max_depth) {
                            // TODO: add json depth validation when supported
                        }
                    } else if constexpr (std::is_same_v<S, TimestampSettings>) {
                        if (settings.default_now && *settings.default_now) {
                            col.default_value("CURRENT_TIMESTAMP");
                        }
                    }

                    return col;
                },
                field.settings);

            if (!column) {
                return std::unexpected(column.error());
            }

            schema.add_column(std::move(*column));
        }

        return schema;
    }

private:
    static ColumnType get_column_type(Type type) {
        switch (type) {
        case Type::Id:
        case Type::Integer:
            return ColumnType::Integer;
        case Type::Float:
            return ColumnType::Real;
        case Type::String:
        case Type::Email:
        case Type::Url:
        case Type::Username:
        case Type::Phone:
        case Type::Ip:
        case Type::Ipv4:
        case Type::Ipv6:
        case Type::Base64:
        case Type::Hex:
            return ColumnType::Text;
        case Type::BigInt:
        case Type::BigFloat:
            return ColumnType::Text;
        case Type::Json:
            return ColumnType::Json;
        case Type::Timestamp:
            return ColumnType::Integer;
        default:
            return ColumnType::Text;
        }
    }

    std::string        m_name;
    std::vector<Field> m_fields;
};
