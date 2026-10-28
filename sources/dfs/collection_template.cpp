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
            if (!column.has_value()) {
                return std::unexpected(column.error());
            }
            schema.add_column(std::move(column.value()));
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

        if (m_is_primary.value_or(false) && m_autoincrement.has_value()) {
            column.primary_key(m_autoincrement.value());
            return column;
        }

        if (m_required.value_or(false)) {
            column.not_null();
        }

        if (m_unique.value_or(false)) {
            column.unique();
        }

        if (m_default_now.value_or(false) && m_type == FieldType::Timestamp) {
            column.default_value("(unixepoch() * 1000)");
        } else if (m_default.has_value()) {
            column.default_literal(m_default.value());
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
                        Dfs::Tables::DirsFile::ActorSpace::get_collection_template_file_id(arg.owner_id,
                                                                                           arg.file_id);
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
