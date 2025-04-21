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

#include "blockchain/actor_id.h"
#include "utils/db_schema.h"
#include "utils/exc_utils.h"
#include <boost/describe.hpp>
#include <expected>
#include <string>
#include <vector>

namespace Dfs {
    enum class FieldType {
        Id,
        ActorId,
        // FileId
        // TransactionLink
        Bool,
        Integer,
        Real,
        String,
        Blob,
        Json,
        Email,
        Url,
        Username,
        Ip,
        Phone,
        Timestamp
    };

    class FieldBuilder {
    public:
        FieldBuilder() = default;

        FieldBuilder& primary_key(SqlAutoincrement autoincrement = SqlAutoincrement::Yes) {
            if (m_name != "id")
                return *this;
            m_is_primary    = true;
            m_autoincrement = autoincrement;
            return *this;
        }

        FieldBuilder& not_null() {
            m_required = true;
            return *this;
        }

        FieldBuilder& unique() {
            m_unique = true;
            return *this;
        }

        FieldBuilder& default_value(std::string value) {
            m_default = std::move(value);
            return *this;
        }

        template <typename T>
        FieldBuilder& default_value(const T& value) {
            if constexpr (std::is_arithmetic_v<T>) {
                m_default = std::to_string(value);
            } else {
                m_default = Json::serialize(value);
            }
            return *this;
        }

        FieldBuilder& between(std::string min, std::string max) {
            m_min = std::move(min);
            m_max = std::move(max);
            return *this;
        }

        FieldBuilder& between(int64_t min, int64_t max) {
            m_min = std::to_string(min);
            m_max = std::to_string(max);
            return *this;
        }

        FieldBuilder& length(size_t min, size_t max) {
            m_min_length = min;
            m_max_length = max;
            return *this;
        }

        FieldBuilder& pattern(std::string pattern) {
            m_pattern = std::move(pattern);
            return *this;
        }

        template <typename T>
        FieldBuilder& allowed_values(const std::vector<T>& values) {
            std::vector<std::string> str_values;
            str_values.reserve(values.size());
            for (const auto& value : values) {
                if constexpr (std::is_arithmetic_v<T>) {
                    str_values.push_back(std::to_string(value));
                } else {
                    str_values.push_back(Json::serialize(value));
                }
            }
            m_allowed_values = std::move(str_values);
            return *this;
        }

        FieldBuilder& max_depth(size_t depth) {
            m_max_depth = depth;
            return *this;
        }

        FieldBuilder& default_now() {
            m_default_now = true;
            return *this;
        }

        void set_type(FieldType type) {
            m_type = type;
        }

        std::expected<DbColumn, SqlCreateError> to_db_column() const;

        bool operator==(const FieldBuilder&) const = default;

        const std::string& name() const {
            return m_name;
        }

    private:
        friend struct Field;

        FieldBuilder(std::string name, FieldType type)
            : m_name(std::move(name))
            , m_type(type) {
        }

        std::string                             m_name;
        FieldType                               m_type;
        std::optional<bool>                     m_required;
        std::optional<bool>                     m_unique;
        std::optional<bool>                     m_is_primary;
        std::optional<SqlAutoincrement>         m_autoincrement;
        std::optional<std::string>              m_default;
        std::optional<std::string>              m_min;
        std::optional<std::string>              m_max;
        std::optional<size_t>                   m_min_length;
        std::optional<size_t>                   m_max_length;
        std::optional<std::string>              m_pattern;
        std::optional<std::vector<std::string>> m_allowed_values;
        std::optional<size_t>                   m_max_depth;
        std::optional<bool>                     m_default_now;

        BOOST_DESCRIBE_CLASS(FieldBuilder,
                             (),
                             (),
                             (),
                             (m_name,
                              m_type,
                              m_required,
                              m_unique,
                              m_is_primary,
                              m_autoincrement,
                              m_default,
                              m_min,
                              m_max,
                              m_min_length,
                              m_max_length,
                              m_pattern,
                              m_allowed_values,
                              m_max_depth,
                              m_default_now));
    };

    struct Field {
        // TODO: ActorId
        static FieldBuilder Id(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Id);
        }
        static FieldBuilder Integer(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Integer);
        }
        static FieldBuilder Bool(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Bool).between(0, 1);
        }
        static FieldBuilder Real(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Real);
        }
        static FieldBuilder ActorId(std::string name) {
            return FieldBuilder(std::move(name), FieldType::ActorId)
                .length(BlockchainConst::ACTOR_SIZE, BlockchainConst::ACTOR_SIZE);
        }
        static FieldBuilder String(std::string name) {
            return FieldBuilder(std::move(name), FieldType::String);
        }
        static FieldBuilder Blob(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Blob);
        }
        static FieldBuilder Json(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Json);
        }
        static FieldBuilder Email(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Email);
        }
        static FieldBuilder Url(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Url);
        }
        static FieldBuilder Username(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Username);
        }
        static FieldBuilder Ip(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Ip);
        }
        static FieldBuilder Phone(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Phone);
        }
        static FieldBuilder Timestamp(std::string name) {
            return FieldBuilder(std::move(name), FieldType::Timestamp);
        }
    };

    class CollectionTemplate {
    public:
        CollectionTemplate() = default;
        static std::expected<CollectionTemplate, SqlCreateError> create(std::string name);
        CollectionTemplate& add_fields(const std::initializer_list<FieldBuilder>& fields);
        CollectionTemplate& preadd_fields(const std::initializer_list<FieldBuilder>& fields);
        CollectionTemplate& use_id();
        std::expected<DbSchema, SqlCreateError> to_db_schema() const;

        const std::string                name() const;
        const std::vector<FieldBuilder>& fields() const {
            return m_fields;
        }

        void set_to_blob() {
            for (auto& field : m_fields) {
                field.set_type(FieldType::Blob);
            }
        }

        void set_actor_file(const ActorId& actor_id, const std::string file_id);

        bool operator==(const CollectionTemplate&) const = default;

        BOOST_DESCRIBE_CLASS(CollectionTemplate, (), (), (), (m_name, m_fields, primary));
        // no need actor_id or file_id

        std::optional<FieldBuilder> primary;

    private:
        explicit CollectionTemplate(std::string name);

        std::string               m_name;
        std::vector<FieldBuilder> m_fields;
        ActorId                   actor_id;
        std::string               file_id;

        friend class FieldBuilder;
    };

    struct CollectionTemplateLink {
        ActorId     owner_id;
        std::string file_id;
        std::string name;
    };
    BOOST_DESCRIBE_STRUCT(CollectionTemplateLink, (), (owner_id, file_id, name))

    using DfsTemplateVariant = std::variant<CollectionTemplateLink, Dfs::CollectionTemplate>;

    std::optional<std::pair<Dfs::CollectionTemplate, bool>> read_template_from_variant(
        const DfsTemplateVariant& var);
} // namespace Dfs
