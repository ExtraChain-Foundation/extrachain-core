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

#include "dfs/dfs_utils.h"
#include "chain/actor.h"
#include "utils/fs_path.h"

enum class CollectionOperation {
    Structural,
    StructuralTemplated,
    Add,
    Update,
    Remove
};

enum class CollectionError {
    Unknown,
    CollectionNotFound,
    CollectionEmpty,
    InvalidHistory,
    Conflict,
    HistoryNotFound,
    StructuralCreation,
    Adding,
    Updating,
    Deleting,
    IncorrectEncryption
};

struct HistoricalCollectionRow {
    uint32_t                id = 0;
    std::optional<uint32_t> prev_id;
    std::string             prev_hash;
    std::optional<uint32_t> target_id;
    CollectionOperation     operation = CollectionOperation::Structural;
    std::string             data;
    std::uint64_t           timestamp = 0;
    ActorId                 actor_id;
    Signature               sign = Signature();
};
BOOST_DESCRIBE_STRUCT(HistoricalCollectionRow,
                      (),
                      (id, prev_id, prev_hash, target_id, operation, data, timestamp, actor_id, sign))

namespace ExtraChain::Core {
    class ExtraChainNode;
}

class HistoricalCollection {
private:
    ExtraChain::Core::ExtraChainNode* node;
    FsPath                            file_path_;
    Actor<KeyPrivate>     actor_;
    ActorId               file_actor_id_;
    std::string           file_id_;
    std::string           table_name_;
    Dfs::DataSecurity     data_security_;
    Dfs::DataSecurityData security_data_;

    HistoricalCollection() = default;
    HistoricalCollection(ExtraChain::Core::ExtraChainNode* node,
                         const Actor<KeyPrivate>&          actor,
                         const ActorId&                    file_actor_id,
                         const std::string&                file_id,
                         Dfs::DataSecurity                 data_security,
                         const Dfs::DataSecurityData&      security_data);

public:
    static std::expected<HistoricalCollection, CollectionError> create(
        ExtraChain::Core::ExtraChainNode* node,
        const Actor<KeyPrivate>&          main_actor,
        const ActorId&                    file_actor_id,
        const std::string&                file_id,
        const ActorId&                    template_actor_id,
        const std::string&                template_file_id,
        Dfs::DataSecurity                 data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData&      security_data = Dfs::DataSecurityData());
    static std::expected<HistoricalCollection, CollectionError> create(
        ExtraChain::Core::ExtraChainNode* node,
        const Actor<KeyPrivate>&          main_actor,
        const ActorId&                    file_actor_id,
        const std::string&                file_id,
        const Dfs::CollectionTemplate&    collection_template,
        Dfs::DataSecurity                 data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData&      security_data = Dfs::DataSecurityData());

    static std::expected<HistoricalCollection, CollectionError> load(
        ExtraChain::Core::ExtraChainNode* node,
        const Actor<KeyPrivate>&          actor,
        const ActorId&                    file_actor_id,
        const std::string&                file_id,
        Dfs::DataSecurity                 data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData&      security_data = Dfs::DataSecurityData());

    std::expected<HistoricalCollectionRow, CollectionError> add_row(const DbRow&                 row,
                                                                    Dfs::DataSecurity            data_security,
                                                                    const Dfs::DataSecurityData& security_data);
    std::expected<HistoricalCollectionRow, CollectionError> update_row(const uint32_t               id,
                                                                       const DbRow&                 row,
                                                                       Dfs::DataSecurity            data_security,
                                                                       const Dfs::DataSecurityData& security_data);
    std::expected<HistoricalCollectionRow, CollectionError> remove_row(const uint32_t id);

    std::expected<void, CollectionError> change_collection(const HistoricalCollectionRow& historical_row);

    // void do_something

    // std::expected<HistoricalCollectionRow, CollectionError> insert_into_alien(DbRow&             row,
    //                                                                           const std::string& temp_table);

    std::expected<std::vector<DbRow>, CollectionError> get_collection_rows(
        const std::string& where_statement = "");

    static constexpr std::size_t MaxEventBytes = 1024 * 1024;
    static constexpr std::size_t MaxPageBytes  = 4 * 1024 * 1024;
    static constexpr std::size_t MaxPageRows   = 128;

    std::expected<std::vector<HistoricalCollectionRow>, CollectionError> get_historical_rows(
        std::uint64_t after = 0,
        std::size_t   limit = MaxPageRows);
    static std::expected<bool, CollectionError> accept(ExtraChain::Core::ExtraChainNode*           node,
                                                       const ActorId&                              owner,
                                                       const std::string&                          file,
                                                       const std::vector<HistoricalCollectionRow>& rows);
    static std::string row_hash(const ActorId& owner, const std::string& file, const HistoricalCollectionRow& row);
    static bool        verify(ExtraChain::Core::ExtraChainNode* node,
                              const ActorId&                    owner,
                              const std::string&                file,
                              const HistoricalCollectionRow&    row);
    static std::pair<std::string, std::uint64_t>            hash_size(DbConnector& db);
    std::expected<HistoricalCollectionRow, CollectionError> get_row(const std::string& search_value,
                                                                    const std::string& field = "id");

    std::expected<HistoricalCollectionRow, CollectionError> get_last_row();
    std::expected<std::variant<Dfs::CollectionTemplateLink, Dfs::CollectionTemplate>, CollectionError>
    get_creation();

    FsPath get_historical_path() const;
    FsPath get_file_path() const;

private:
    Dfs::CollectionTemplate                                 schema_;
    std::expected<HistoricalCollectionRow, CollectionError> mutate(CollectionOperation          operation,
                                                                   std::optional<std::uint32_t> target,
                                                                   const DbRow&                 data,
                                                                   Dfs::DataSecurity            security,
                                                                   const Dfs::DataSecurityData& security_data);
    std::expected<DbRow, CollectionError> encrypt_data(const DbRow&                 row,
                                                       Dfs::DataSecurity            data_security,
                                                       const Dfs::DataSecurityData& security_data);
    std::expected<DbRow, CollectionError> decrypt_data(const DbRow&                 row,
                                                       Dfs::DataSecurity            data_security,
                                                       const Dfs::DataSecurityData& security_data);
};
