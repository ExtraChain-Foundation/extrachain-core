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

#include "dfs/collection_template.h"
#include "dfs/dfs_utils.h"
#include <boost/algorithm/string/join.hpp>

namespace Dfs {
    std::expected<CollectionTemplate, SqlCreateError> CollectionTemplate::create(std::string name) {
        // TODO: CamelCase, A-Za-z
        auto tmpl = CollectionTemplate(std::move(name));
        tmpl.add_fields({
            // Field::Id("id").primary_key(),
            // Field::Integer("timestamp").not_null(),
            /*
            Field::Text("actor_id").not_null().unique(),
            Field::Text("sign").not_null().unique()
            */
        });
        return tmpl;
    }

    CollectionTemplate::CollectionTemplate(std::string name)
        : m_name(std::move(name)) {
    }

    CollectionTemplate& CollectionTemplate::add_fields(const std::initializer_list<FieldBuilder>& fields) {
        m_fields.insert(m_fields.end(), fields);
        return *this;
    }

    CollectionTemplate& CollectionTemplate::preadd_fields(const std::initializer_list<FieldBuilder>& fields) {
        m_fields.insert(m_fields.begin(), fields);
        return *this;
    }

    CollectionTemplate& CollectionTemplate::use_id() {
        this->primary = Dfs::Field::String("id").unique().not_null();
        return *this;
    }

    std::expected<DbSchema, SqlCreateError> CollectionTemplate::to_db_schema() const {
        DbSchema schema(m_name);

        for (const auto& field : m_fields) {
            auto column = field.to_db_column();
            if (!column) {
                return std::unexpected(column.error());
            }
            schema.add_column(std::move(*column));
        }

        return schema;
    }

    std::expected<DbColumn, SqlCreateError> FieldBuilder::to_db_column() const {
        auto map_type_to_column = [](FieldType type) -> ColumnType {
            switch (type) {
            case FieldType::Id:
            case FieldType::Integer:
            case FieldType::Bool:
            case FieldType::Timestamp:
                return ColumnType::Integer;
            case FieldType::Real:
                return ColumnType::Real;
            case FieldType::Json:
                return ColumnType::Json;
            case FieldType::Blob:
                return ColumnType::Blob;
            default:
                return ColumnType::Text;
            }
        };

        DbColumn column(m_name, map_type_to_column(m_type));

        if (m_is_primary && m_autoincrement.has_value()) {
            column.primary_key(m_autoincrement.value());
            return column;
        }

        if (m_required) {
            column.not_null();
        }

        if (m_unique) {
            column.unique();
        }

        if (m_default_now && m_type == FieldType::Timestamp) {
            column.default_value("CURRENT_TIMESTAMP");
        } else if (m_default) {
            column.default_value(*m_default);
        }

        std::vector<std::string> checks;

        // Length checks for string types
        if ((m_min_length || m_max_length)
            && (m_type == FieldType::String || m_type == FieldType::ActorId || m_type == FieldType::Email
                || m_type == FieldType::Url || m_type == FieldType::Username)) {
            checks.push_back(fmt::format("length({}) BETWEEN {} AND {}",
                                         m_name,
                                         m_min_length.value_or(0),
                                         m_max_length.value_or(std::numeric_limits<size_t>::max())));
        }

        // Range checks for numeric types
        if ((m_min || m_max)
            && (m_type == FieldType::Integer || m_type == FieldType::Real || m_type == FieldType::Bool)) {
            if (m_min) {
                checks.push_back(fmt::format("{} >= {}", m_name, *m_min));
            }
            if (m_max) {
                checks.push_back(fmt::format("{} <= {}", m_name, *m_max));
            }
        }

        // Pattern check
        if (m_pattern) {
            checks.push_back(fmt::format("{} REGEXP '{}'", m_name, *m_pattern));
        }

        // Allowed values check
        if (m_allowed_values && !m_allowed_values->empty()) {
            std::vector<std::string> quoted;
            quoted.reserve(m_allowed_values->size());
            for (const auto& val : *m_allowed_values) {
                quoted.push_back(fmt::format("'{}'", val));
            }
            checks.push_back(fmt::format("{} IN ({})", m_name, boost::algorithm::join(quoted, ", ")));
        }

        // Add combined checks if any exist
        if (!checks.empty()) {
            // column.check(boost::algorithm::join(checks, " AND "));
        }

        return column;
    }

    const std::string CollectionTemplate::name() const {
        return m_name;
    }

    void CollectionTemplate::set_actor_file(const ActorId& actor_id, const std::string file_id) {
        this->actor_id = actor_id;
        this->file_id  = file_id;
    }

    std::optional<std::pair<Dfs::CollectionTemplate, bool>> read_template_from_variant(
        const DfsTemplateVariant& var) {
        return std::visit(
            [](auto&& arg) -> std::optional<std::pair<Dfs::CollectionTemplate, bool>> {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (std::is_same_v<T, CollectionTemplateLink>) {
                    auto vector_template =
                        Dfs::Tables::DirsFile::ActorSpace::get_collection_template_file_id(arg.owner_id, arg.file_id);
                    if (!vector_template.has_value()) {
                        return std::nullopt;
                    }
                    return std::pair<Dfs::CollectionTemplate, bool>(vector_template.value(), true);
                } else if constexpr (std::is_same_v<T, Dfs::CollectionTemplate>) {
                    return std::pair<Dfs::CollectionTemplate, bool>(arg, false);
                }
            },
            var);
    }
} // namespace Dfs
