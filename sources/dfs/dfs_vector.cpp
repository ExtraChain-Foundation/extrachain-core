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

#include "dfs/dfs_vector.h"

#include "utils/exc_utils.h"

DfsVector::DfsVector(ExtraChainNode              *node,
                     const Actor<KeyPrivate>     &actor,
                     const ActorId               &file_actor_id,
                     const std::string           &file_id,
                     Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
                     const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData()) {
    this->node           = node;
    this->file_path_     = Dfs::Path::file_path(file_actor_id, file_id).value();
    this->vector_path_   = FsPath::create(this->file_path_.native().string() + Dfs::Basic::VECTOR_FILE).value();
    this->actor_         = actor;
    this->file_actor_id_ = file_actor_id;
    this->file_id_       = file_id;
    this->data_security_ = data_security;
    this->security_data_ = security_data;
}

// std::expected<DfsVector, DfsVectorError> DfsVector::create(ExtraChainNode              *node,
//                                                            const Actor<KeyPrivate>     &main_actor,
//                                                            const ActorId               &file_actor_id,
//                                                            const std::string           &file_id,
//                                                            const ActorId               &template_actor_id,
//                                                            const std::string           &template_file_id,
//                                                            Dfs::DataSecurity            data_security,
//                                                            const Dfs::DataSecurityData &security_data) {
//     auto vector_template =
//         Dfs::Tables::ActorDirFile::get_collection_template_file_id(template_actor_id, template_file_id);
//     if (!vector_template.has_value()) {
//         return std::unexpected(DfsVectorError::Unknown);
//     }

//     auto dfs_vector = create(node,
//                              main_actor,
//                              file_actor_id,
//                              file_id,
//                              vector_template.value(),
//                              data_security,
//                              security_data,
//                              false);

//     if (!dfs_vector.has_value()) {
//         return std::unexpected(DfsVectorError::Unknown);
//     }

//     auto link     = CollectionTemplateLink { .actor_id = template_actor_id,
//                                              .file_id  = template_file_id,
//                                              .name     = vector_template->name() };
//     auto json     = Json::serialize(link);
//     auto res_json = Utils::write_file_content(dfs_vector->vector_path_, std::move(json));
//     if (!res_json.has_value()) {
//         return std::unexpected(DfsVectorError::Unknown);
//     }

//     return std::unexpected(DfsVectorError::Unknown);
// }

std::expected<DfsVector, DfsVectorError> DfsVector::create(ExtraChainNode                *node,
                                                           const Actor<KeyPrivate>       &main_actor,
                                                           const ActorId                 &file_actor_id,
                                                           const std::string             &file_id,
                                                           const Dfs::DfsTemplateVariant &variant_template,
                                                           Dfs::DataSecurity              data_security,
                                                           const Dfs::DataSecurityData   &security_data) {
    DfsVector dfs_vector(node, main_actor, file_actor_id, file_id, data_security, security_data);

    // TODO: if vector template has actor, status, timestamp or sign -> error

    auto from_template_result = read_template_from_variant(variant_template);
    if (!from_template_result.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto [vector_template, is_link] = from_template_result.value();
    dfs_vector.collection_template_ = vector_template;

    if (vector_template.primary.has_value()) {
        const auto &primary = vector_template.primary.value();
        vector_template.preadd_fields({ primary,
                                        Dfs::Field::ActorId("actor").not_null(),
                                        Dfs::Field::Blob("sign").not_null(),
                                        Dfs::Field::Timestamp("timestamp").not_null(),
                                        Dfs::Field::Integer("status").not_null() });
    } else {
        vector_template.preadd_fields({ Dfs::Field::ActorId("actor").unique().not_null(),
                                        Dfs::Field::Blob("sign").not_null(),
                                        Dfs::Field::Timestamp("timestamp").not_null(),
                                        Dfs::Field::Integer("status").not_null() });
    }

    auto schema = vector_template.to_db_schema();
    if (!schema.has_value()) {
        return std::unexpected(DfsVectorError::StructuralCreation);
    }

    schema->set_table_name("Vector");

    if (!is_link) {
        auto json     = Json::serialize(vector_template);
        auto res_json = Utils::write_file_content(dfs_vector.vector_path_, std::move(json));
        if (!res_json.has_value()) {
            return std::unexpected(DfsVectorError::Unknown);
        }
    } else {
        if (std::holds_alternative<Dfs::CollectionTemplateLink>(variant_template)) {
            auto link = std::get<Dfs::CollectionTemplateLink>(variant_template);
            link.name = vector_template.name();

            auto json     = Json::serialize(link);
            auto res_json = Utils::write_file_content(dfs_vector.vector_path_, std::move(json));
            if (!res_json.has_value()) {
                return std::unexpected(DfsVectorError::Unknown);
            }
        }
    }

    DbConnector db(dfs_vector.file_path_);
    db.open();
    auto res_create = db.create_table(schema.value());
    db.close();

    if (!res_create.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    return dfs_vector;
}

std::expected<DfsVector, DfsVectorError> DfsVector::load(ExtraChainNode              *node,
                                                         const Actor<KeyPrivate>     &actor,
                                                         const ActorId               &file_actor_id,
                                                         const std::string           &file_id,
                                                         Dfs::DataSecurity            data_security,
                                                         const Dfs::DataSecurityData &security_data) {
    if (file_actor_id.is_zero() || file_id.empty()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    DfsVector dfs_vector(node, actor, file_actor_id, file_id, data_security, security_data);

    auto vector_template = dfs_vector.read_template();
    if (!vector_template.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    dfs_vector.collection_template_ = vector_template.value();
    // checks

    return dfs_vector;
}

std::expected<DfsVector, DfsVectorError> DfsVector::load_network(ExtraChainNode              *node,
                                                                 const Actor<KeyPrivate>     &actor,
                                                                 const ActorId               &file_actor_id,
                                                                 const std::string           &file_id,
                                                                 Dfs::DataSecurity            data_security,
                                                                 const Dfs::DataSecurityData &security_data) {
    DfsVector dfs_vector(node, actor, file_actor_id, file_id, data_security, security_data);
    return dfs_vector;
}

std::expected<DbRow, DfsVectorError> DfsVector::read_row(const ActorId &actor_id) {
    DbConnector db(file_path_);
    db.open();
    if (!db.is_open()) {
        return std::unexpected(DfsVectorError::CollectionNotFound);
    }

    auto query = fmt::format("SELECT * FROM {} WHERE actor = '{}' AND status = '1'", "Vector", actor_id);
    std::vector<DbRow> db_rows = db.select(query);

    if (db_rows.empty()) {
        return std::unexpected(DfsVectorError::CollectionEmpty);
    }

    db.close();

    return db_rows.front();
}

std::expected<std::vector<DbRow>, DfsVectorError> DfsVector::read_rows(const std::string &where_statement) {
    DbConnector db(file_path_);
    db.open();
    if (!db.is_open()) {
        return std::unexpected(DfsVectorError::CollectionNotFound);
    }

    auto               query   = fmt::format("SELECT * FROM {} {}", "Vector", where_statement);
    std::vector<DbRow> db_rows = db.select(query);

    if (db_rows.empty()) {
        return std::unexpected(DfsVectorError::CollectionEmpty);
    }

    db.close();

    return db_rows;
}

std::expected<Dfs::CollectionTemplate, DfsVectorError> DfsVector::read_template() {
    auto content = Utils::read_file_content(vector_path_);
    if (!content.has_value()) {
        eCritical("[DfsVector] Can't find {}", vector_path_.native());
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto vector_template = Json::deserialize<Dfs::CollectionTemplate>(content.value());

    if (vector_template.has_value()) {
        if (vector_template->fields().size() != 0) {
            return vector_template.value();
        }

        auto vector_template_link = Json::deserialize<Dfs::CollectionTemplateLink>(content.value());
        if (!vector_template_link.has_value()) {
            return std::unexpected(DfsVectorError::Unknown);
        }

        auto vector_template_file_id =
            Dfs::Tables::ActorDirFile::get_collection_template_file_id(vector_template_link->owner_id,
                                                                       vector_template_link->file_id);

        if (!vector_template_file_id.has_value()) {
            return std::unexpected(DfsVectorError::Unknown);
        }

        return vector_template_file_id.value();
    }

    return vector_template.value();
}

std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> DfsVector::generate_content_package(
    const std::string &where_statement) {

    auto rows = read_rows("");

    auto res = Utils::read_file_content(vector_path_);
    if (!res.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto vector_template = read_template();
    if (!vector_template.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    return Dfs::Packets::DfsVectorContentPackage { .owner_id        = file_actor_id_,
                                                   .file_id         = file_id_,
                                                   .vector_template = vector_template.value(),
                                                   .vector_file     = ByteArray(res.value()).toString(),
                                                   .content =
                                                       rows.has_value() ? rows.value() : std::vector<DbRow> {} };
}

bool DfsVector::handle_package(const Dfs::Packets::DfsVectorContentPackage &dfs_vector_content) {
    // TODO: move this to create?
    // auto json     = Json::serialize(dfs_vector_content.vector_template);
    auto res_json = Utils::write_file_content(vector_path_, dfs_vector_content.vector_file);
    if (!res_json.has_value()) {
        return false;
    }

    auto vector_template = dfs_vector_content.vector_template;
    if (vector_template.fields().size() == 0) {
        return false;
    }

    if (vector_template.primary.has_value()) {
        const auto &primary = vector_template.primary.value();
        vector_template.preadd_fields({ primary,
                                        Dfs::Field::ActorId("actor").not_null(),
                                        Dfs::Field::Blob("sign").not_null(),
                                        Dfs::Field::Timestamp("timestamp").not_null(),
                                        Dfs::Field::Integer("status").not_null() });
    } else {
        vector_template.preadd_fields({ Dfs::Field::ActorId("actor").unique().not_null(),
                                        Dfs::Field::Blob("sign").not_null(),
                                        Dfs::Field::Timestamp("timestamp").not_null(),
                                        Dfs::Field::Integer("status").not_null() });
    }

    auto schema = vector_template.to_db_schema();
    if (!schema.has_value()) {
        return false;
    }

    schema->set_table_name("Vector");

    DbConnector db(file_path_);
    db.open();
    db.create_table(schema.value());

    for (const auto &db_row : dfs_vector_content.content) {
        db.replace("Vector", db_row);
    }

    db.close();
    return true;
}

bool DfsVector::store_add(DbRow &row) {
    row["timestamp"] = std::to_string(Utils::current_date_ms());
    if (row["status"] != "0") {
        row["status"] = "1";
    }

    auto [hash, all_empty] = calculate_hash(row);
    if (hash.empty() || all_empty) {
        return false;
    }

    auto sign = actor_.key().sign(hash);
    if (!sign.has_value()) {
        return false;
    }

    row["actor"] = actor_.id().to_string();
    row["sign"]  = ByteArray(sign.value()).toString();
    auto res     = local_add(row, false);
    return res;
}

bool DfsVector::local_add(const DbRow &row, bool check) {
    bool verify = this->verify(row);
    if (!verify) {
        return false;
    }

    if (check) {
        auto exrow = read_row(ActorId(row.at("actor")));
        if (exrow.has_value()) {
            auto extimestamp = std::stoull(exrow->at("timestamp"));
            auto timestamp   = std::stoull(row.at("timestamp"));
            if (extimestamp > timestamp) {
                return true;
            }
        }
    }

    DbConnector db(file_path_);
    if (!db.open()) {
        return false;
    }
    bool res = db.replace("Vector", row);
    db.close();
    return res;
}

std::optional<DbRow> DfsVector::remove(const ActorId &actor_id) {
    auto row_result = read_row(actor_id);
    if (!row_result.has_value()) {
        return std::nullopt;
    }

    auto row = std::move(row_result.value());
    for (const auto &[key, _] : row) {
        row[key] = "-";
    }

    row["status"] = "0";

    auto res = store_add(row);
    // bool res = db.delete_row("Vector", row);
    if (!res) {
        return std::nullopt;
    }

    return row;
}

std::pair<std::string, bool> DfsVector::calculate_hash(const DbRow &row) {
    // TODO: try..catch
    std::string to_hash   = row.at("status") + row.at("timestamp") + file_actor_id_.to_string() + file_id_;
    bool        all_empty = true;

    if (to_hash.size() != 14 + 40 + 64) { // 1 + 13 + 40 + 64
        return { "", true };
    }

    const auto &fields = collection_template_.fields();
    for (const auto &field : fields) {
        std::string value = row.at(field.name());
        if (!value.empty()) {
            all_empty = false;
        }

        to_hash += value;
    }

    if (all_empty || to_hash.empty()) {
        return { "", true };
    }

    auto hash = Utils::calculate_hash(to_hash);
    return { hash, false };
}

bool DfsVector::verify(const DbRow &row) {
    auto      actor_id = ActorId(row.at("actor"));
    auto      actor    = node->actorIndex()->getActor(actor_id);
    Signature sign     = ByteArray(row.at("sign")).toArray<crypto_sign_BYTES>();

    auto [hash, all_empty] = calculate_hash(row);
    if (hash.empty() || all_empty) {
        return false;
    }

    auto verify = actor.key().verify(hash, sign);
    if (!verify.has_value()) {
        return false;
    }

    return verify.value();
}
