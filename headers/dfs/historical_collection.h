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

#include "utils/db_schema.h"
#include "dfs/dfs_utils.h"
#include "blockchain/actor.h"
#include "utils/fs_path.h"

enum class CollectionOperation {
    Structural,
    Add,
    Update,
    Remove
};

enum class CollectionError {
    Unknown,
    CollectionNotFound,
    HistoryNotFound,
    StructuralCreation,
    Adding,
    Updating,
    Deleting
};

struct HistoricalCollectionRow {
    uint32_t                id = 0;
    std::optional<uint32_t> prev_id;
    CollectionOperation     operation = CollectionOperation::Structural;
    std::string             data;
    std::uint64_t           timestamp = 0;
    ActorId                 actor_id;
    Signature               sign = Signature();
};
BOOST_DESCRIBE_STRUCT(HistoricalCollectionRow, (), (id, prev_id, operation, data, timestamp, actor_id, sign))

struct CollectionTemplateLink {
    ActorId     actor_id;
    std::string file_id;
    std::string name;
};
BOOST_DESCRIBE_STRUCT(CollectionTemplateLink, (), (actor_id, file_id, name))

class HistoricalCollection {
private:
    FsPath                             file_path_;
    FsPath                             historical_path_;
    std::shared_ptr<Actor<KeyPrivate>> actor_;
    ActorId                            file_actor_id_;
    std::string                        file_id_;
    std::string                        table_name_;

    HistoricalCollection(const std::shared_ptr<Actor<KeyPrivate>>& actor,
                         const ActorId&                            file_actor_id,
                         const std::string&                        file_id);

public:
    static std::expected<HistoricalCollection, CollectionError> create(
        const std::shared_ptr<Actor<KeyPrivate>>& main_actor,
        const ActorId&                            file_actor_id,
        const std::string&                        file_id,
        const ActorId&                            tempalte_actor_id,
        const std::string&                        template_file_id);
    static std::expected<HistoricalCollection, CollectionError> load(
        const std::shared_ptr<Actor<KeyPrivate>>& actor,
        const ActorId&                            file_actor_id,
        const std::string&                        file_id);

    std::expected<HistoricalCollectionRow, CollectionError> add_row(DbRow& row);
    std::expected<HistoricalCollectionRow, CollectionError> update_row(std::uint32_t id, DbRow& row);
    std::expected<HistoricalCollectionRow, CollectionError> remove_row(std::uint32_t id);

    // std::expected<HistoricalCollectionRow, CollectionError> insert_into_alien(DbRow&             row,
    //                                                                           const std::string& temp_table);

    std::expected<std::vector<DbRow>, CollectionError>                   get_collection_rows();
    std::expected<std::vector<HistoricalCollectionRow>, CollectionError> get_historical_rows() {
        DbConnector db(historical_path_);
        db.open();
        if (!db.is_open()) {
            return std::unexpected(CollectionError::HistoryNotFound);
        }

        std::vector<HistoricalCollectionRow> rows;

        std::vector<DbRow> db_rows = db.select(fmt::format("SELECT * FROM {}", Dfs::Historical::HISTORICAL_TABLE));
        db.close();

        for (auto& row : db_rows) {
            auto dirRow = Utils::from_dbrow<HistoricalCollectionRow>(row);
            if (dirRow.has_value()) {
                rows.push_back(dirRow.value());
            }
        }

        return rows;
    }
    std::expected<HistoricalCollectionRow, CollectionError> get_row(const std::string& search_value,
                                                                    const std::string& field = "id");

    std::expected<HistoricalCollectionRow, CollectionError> get_last_row();
    std::expected<CollectionTemplateLink, CollectionError>  get_creation();

    FsPath get_historical_path() const;
    FsPath get_file_path() const;

private:
    std::expected<std::string, CollectionError> create_table(const ActorId&     tempalte_actor_id,
                                                             const std::string& template_file_id);

    void insert_historical_row(HistoricalCollectionRow& historical_row);
    void historical_collection_row_sign(HistoricalCollectionRow& row);
    bool historical_collection_row_verify(const HistoricalCollectionRow& row);
};
