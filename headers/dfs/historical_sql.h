/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

#include <filesystem>
#include "utils/exc_magic.h"
#include "utils/db_schema.h"
#include "dfs/dfs_utils.h"
#include "blockchain/actor.h"

enum class HistoricalSqlOperation {
    Create,
    Insert,
    Update,
    Remove
};

struct HistoricalRow {
    std::string            id;
    std::string            prevId;
    HistoricalSqlOperation operation;
    std::string            data;
    std::uint64_t          timestamp;
    ActorId                actorId;
    Signature              sign;
};
BOOST_DESCRIBE_STRUCT(HistoricalRow, (), (id, prevId, operation, data, timestamp, actorId, sign))
MAKE_MAGICAL(HistoricalRow)

class HistoricalSql {
private:
    std::filesystem::path              file_path;
    std::filesystem::path              history_path;
    std::shared_ptr<Actor<KeyPrivate>> actor;
    std::string                        file_id;

    HistoricalSql(const std::shared_ptr<Actor<KeyPrivate>>& actor, const std::string& fileId) {
        auto dfsPath       = DfsPath::filePath(actor->id(), fileId);
        this->file_path    = dfsPath;
        this->history_path = fmt::format("{}.history", dfsPath, ".history");
        this->actor        = actor;
        this->file_id      = fileId;
    }

public:
    static HistoricalSql create(const std::shared_ptr<Actor<KeyPrivate>>& actor, const std::string& file_id);
    static HistoricalSql load(const std::shared_ptr<Actor<KeyPrivate>>& actor, const std::string& file_id);

    std::expected<std::string, SqlCreateError> create_table(const DbSchema& schema);

    std::expected<void, SqlCreateError> insert_into(DbRow& row, const std::string& temp_table);
    std::expected<void, SqlCreateError> update_where(DbRow& row, const std::string& temp_table);
    std::expected<void, SqlCreateError> delete_where(DbRow& row, const std::string& temp_table);

private:
    void historicalRowSign(HistoricalRow& row);
    bool historicalRowVeryify(const HistoricalRow& row);
};
