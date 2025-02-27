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

std::expected<DfsVector, DfsVectorError> DfsVector::create(ExtraChainNode                *node,
                                                           const Actor<KeyPrivate>       &main_actor,
                                                           const ActorId                 &file_actor_id,
                                                           const std::string             &file_id,
                                                           const Dfs::CollectionTemplate &vector_template,
                                                           Dfs::DataSecurity              data_security,
                                                           const Dfs::DataSecurityData   &security_data) {
    DfsVector dfs_vector(node, main_actor, file_actor_id, file_id, data_security, security_data);

    // TODO: if vector template has actor_id or sign -> error

    auto new_template = vector_template;
    new_template.preadd_fields({ Dfs::Field::ActorId("actor").not_null(), Dfs::Field::Blob("sign").not_null() });

    auto schema = new_template.to_db_schema();
    if (!schema.has_value()) {
        return std::unexpected(DfsVectorError::StructuralCreation);
    }

    schema->set_table_name("Vector");

    auto json     = Json::serialize(vector_template);
    auto res_json = Utils::write_file_content(dfs_vector.vector_path_, std::move(json));
    if (!res_json.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
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
    DfsVector dfs_vector(node, actor, file_actor_id, file_id, data_security, security_data);

    // checks

    return dfs_vector;
}

std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> DfsVector::get_content_package(
    bool               allow_empty,
    const std::string &where_statement) {
    DbConnector db(file_path_);
    db.open();
    if (!db.is_open()) {
        return std::unexpected(DfsVectorError::CollectionNotFound);
    }

    std::vector<DbRow> db_rows = db.select(fmt::format("SELECT * FROM {} {}", "Vector", where_statement));

    if (!allow_empty && db_rows.empty()) {
        return std::unexpected(DfsVectorError::CollectionEmpty);
    }

    auto fields = db.table_columns("Vector");
    db.close();

    auto res = Utils::read_file_content(vector_path_);
    if (!res.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto vector_template = Json::deserialize<Dfs::CollectionTemplate>(res.value());

    return Dfs::Packets::DfsVectorContentPackage { .owner_id        = actor_.id(),
                                                   .file_id         = file_id_,
                                                   .fields          = fields,
                                                   .vector_template = vector_template.value(),
                                                   .content         = db_rows };
}

void DfsVector::create_insert(const Dfs::Packets::DfsVectorContentPackage &dfs_vector_content) {

    auto json     = Json::serialize(dfs_vector_content.vector_template);
    auto res_json = Utils::write_file_content(vector_path_, std::move(json));
    if (!res_json.has_value()) {
        return;
    }

    DbConnector db(file_path_);
    db.open();
    db.create_table("Vector", dfs_vector_content.fields);

    for (const auto &db_row : dfs_vector_content.content) {
        db.insert("Vector", db_row);
    }

    db.close();
}

bool DfsVector::store_add(DbRow &row) {
    std::string temp;
    bool        all_empty = true;
    // TODO: use collection for sort
    for (auto &[key, value] : row) {
        temp += value;

        if (!value.empty()) {
            all_empty = false;
        }
    }

    if (all_empty) {
        return false;
    }

    auto hash = Utils::calculate_hash(temp);
    auto sign = actor_.key().sign(hash);
    if (!sign.has_value()) {
        return false;
    }

    row["actor"] = actor_.id().to_string();
    row["sign"]  = ByteArray(sign.value()).toString();
    auto res     = local_add(row);
    return res;
}

bool DfsVector::local_add(const DbRow &row) {
    DbConnector db(file_path_);
    db.open();
    // check exists id
    bool res = db.insert("Vector", row);
    db.close();
    return res;
}

bool DfsVector::remove(const DbRow &row) {
    DbConnector db(file_path_);
    db.open();
    bool res = db.delete_row("Vector", row);
    db.close();
    return res;
}
