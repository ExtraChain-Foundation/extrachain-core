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
    ExtraChainNode*       node;
    FsPath                file_path_;
    Actor<KeyPrivate>     actor_;
    ActorId               file_actor_id_;
    std::string           file_id_;
    Dfs::DataSecurity     data_security_;
    Dfs::DataSecurityData security_data_;

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
        const Dfs::CollectionTemplate& collection_template,
        Dfs::DataSecurity              data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData&   security_data = Dfs::DataSecurityData());

    static std::expected<DfsVector, DfsVectorError> load(
        ExtraChainNode*              node,
        const Actor<KeyPrivate>&     actor,
        const ActorId&               file_actor_id,
        const std::string&           file_id,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData& security_data = Dfs::DataSecurityData());

    std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> get_rows(
        const std::string& where_statement = "");

    void create_insert(const Dfs::Packets::DfsVectorContentPackage& dfs_vector_content);

    bool add(const DbRow& row);
    bool remove(int id);
};
