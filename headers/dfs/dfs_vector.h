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

#include "managers/extrachain_node.h"
#include "dfs/dfs_utils.h"

enum class DfsVectorError {
    Unknown,
    CollectionNotFound,
    CollectionEmpty,
    HistoryNotFound,
    StructuralCreation,
    Adding,
    Updating,
    Deleting,
    IncorrectEncryption
};

class DfsVector {
private:
    ExtraChainNode*         node;
    FsPath                  file_path_;
    FsPath                  vector_path_;
    Actor<KeyPrivate>       actor_;
    ActorId                 file_actor_id_;
    std::string             file_id_;
    Dfs::DataSecurity       data_security_;
    Dfs::DataSecurityData   security_data_;
    Dfs::CollectionTemplate collection_template_;
    bool                    is_encrypted_;

    DfsVector() = default;
    DfsVector(ExtraChainNode*              node,
              const Actor<KeyPrivate>&     actor,
              const ActorId&               file_actor_id,
              const std::string&           file_id,
              Dfs::DataSecurity            data_security,
              const Dfs::DataSecurityData& security_data);

public:
    // static std::expected<DfsVector, DfsVectorError> create(
    //     ExtraChainNode*              node,
    //     const Actor<KeyPrivate>&     main_actor,
    //     const ActorId&               file_actor_id,
    //     const std::string&           file_id,
    //     const ActorId&               template_actor_id,
    //     const std::string&           template_file_id,
    //     Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
    //     const Dfs::DataSecurityData& security_data = Dfs::DataSecurityData());
    static std::expected<DfsVector, DfsVectorError> create(
        ExtraChainNode*                node,
        const Actor<KeyPrivate>&       main_actor,
        const ActorId&                 file_actor_id,
        const std::string&             file_id,
        const Dfs::DfsTemplateVariant& variant_template,
        Dfs::DataSecurity              data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData&   security_data = Dfs::DataSecurityData());

    static std::expected<DfsVector, DfsVectorError> load(
        ExtraChainNode*              node,
        const Actor<KeyPrivate>&     actor,
        const ActorId&               file_actor_id,
        const std::string&           file_id,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData& security_data = Dfs::DataSecurityData());

    static std::expected<DfsVector, DfsVectorError> load_network(
        ExtraChainNode*              node,
        const Actor<KeyPrivate>&     actor,
        const ActorId&               file_actor_id,
        const std::string&           file_id,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData& security_data = Dfs::DataSecurityData());

    std::expected<DbRow, DfsVectorError> read_row(const ActorId& actor_id);

    std::expected<std::vector<DbRow>, DfsVectorError> read_rows(
        const std::string& where_statement = "where status = '1'");

    std::expected<Dfs::CollectionTemplate, DfsVectorError> read_template();

    std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> generate_content_package(
        const std::string& where_statement = "");

    bool handle_package(const Dfs::Packets::DfsVectorContentPackage& dfs_vector_content);

    bool                 store_add(DbRow& row);
    bool                 local_add(const DbRow& row, bool check);
    std::optional<DbRow> remove(/*const ActorId & actor_id,*/ const ActorId& actor_id);

    std::pair<std::string, bool> calculate_hash(const DbRow& row);

    bool verify(const DbRow& row);

    std::expected<DbRow, DfsVectorError> encrypt_data(const DbRow&                 row,
                                                      const Dfs::DataSecurityData& security_data);

    std::expected<DbRow, DfsVectorError> decrypt_data(const DbRow&                 row,
                                                      const Dfs::DataSecurityData& security_data);
};
