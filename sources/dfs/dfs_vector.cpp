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

DfsVector::DfsVector(ExtraChainNode              *node,
                     const Actor<KeyPrivate>     &actor,
                     const ActorId               &file_actor_id,
                     const std::string           &file_id,
                     Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
                     const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData()) {
    this->node           = node;
    this->file_path_     = Dfs::Path::file_path(file_actor_id, file_id).value();
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
                                                           const Dfs::CollectionTemplate &collection_template,
                                                           Dfs::DataSecurity              data_security,
                                                           const Dfs::DataSecurityData   &security_data) {
    DfsVector dfs_vector(node, main_actor, file_actor_id, file_id, data_security, security_data);

    auto schema = collection_template.to_db_schema();
    if (!schema.has_value()) {
        return std::unexpected(DfsVectorError::StructuralCreation);
    }

    schema->set_table_name("Vector");

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

std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> DfsVector::get_rows(
    const std::string &where_statement) {
    DbConnector db(file_path_);
    db.open();
    if (!db.is_open()) {
        return std::unexpected(DfsVectorError::CollectionNotFound);
    }

    std::vector<DbRow> db_rows =
        db.select(fmt::format("SELECT * FROM {} {} ORDER by id", "Vector", where_statement));

    if (db_rows.empty()) {
        return std::unexpected(DfsVectorError::CollectionEmpty);
    }

    auto fields = db.table_columns("Vector");
    db.close();

    return Dfs::Packets::DfsVectorContentPackage { .fields = fields, .content = db_rows };
}

void DfsVector::create_insert(const Dfs::Packets::DfsVectorContentPackage &dfs_vector_content) {
    DbConnector db(file_path_);
    db.open();
    db.create_table("Vector", dfs_vector_content.fields);

    for (const auto &db_row : dfs_vector_content.content) {
        db.insert("Vector", db_row);
    }

    db.close();
}

bool DfsVector::add(const DbRow &row) {
    DbConnector db(file_path_);
    db.open();
    // check exists id
    bool res = db.insert("Vector", row);
    db.close();
    return res;
}

bool DfsVector::remove(int id) {
    DbConnector db(file_path_);
    db.open();
    bool res = db.delete_row("Vector", { { "id", std::to_string(id) } });
    db.close();
    return res;
}
