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

#include "dfs/dfs_utils.h"

#include "blockchain/actor.h"
#include "utils/fs_path.h"

std::vector<DbRow> Dfs::Tables::ActorDirFile::getFileDataByName(DbConnector *db, std::string name) {
    std::string query = fmt::format("SELECT * FROM {} WHERE file_id = '{}'", TableName, name);
    return db->select(query);
}

std::string Dfs::Tables::ActorDirFile::getLastFileId(DbConnector &db) {
    if (!db.is_open()) {
        eFatal("Database {} not opened", db.file());
    }

    auto        result       = db.select(Dfs::Tables::filesTableLast);
    auto        prevRowOpt   = result.empty() ? std::optional<DbRow> {} : result[0];
    std::string lastFileName = prevRowOpt ? prevRowOpt->at("file_id") : "";
    return lastFileName;
}

DbConnector Dfs::Tables::ActorDirFile::get_actor_dir_file(const ActorId &actorId) {
    DbConnector db(actorDbPath(actorId).string());
    db.open();
    return db;
}

std::filesystem::path Dfs::Tables::ActorDirFile::actorDbPath(const ActorId &actorId) {
    std::string path = DfsB::fsActrRoot + Utils::platformDelimeter() + actorId.to_string()
                       + Utils::platformDelimeter() + DfsB::fsMapName;
    return path;
}

std::filesystem::path Dfs::Tables::ActorDirFile::storjDbPath(const ActorId     &actorId,
                                                             const std::string &storjName) {
    std::string path = DfsB::fsActrRoot + Utils::platformDelimeter() + actorId.to_string()
                       + Utils::platformDelimeter() + storjName;
    return path;
}

std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> Dfs::Tables::ActorDirFile::get_dir_rows(
    const ActorId &actorId,
    std::uint64_t  last_modified) {
    auto db = get_actor_dir_file(actorId);
    if (!db.is_open()) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    std::vector<Dfs::DirRow> dirRows;
    auto                     actrDirData =
        db.select(fmt::format("SELECT * FROM {} WHERE last_modified >= {}", TableName, last_modified));

    for (auto &row : actrDirData) {
        auto dirRow = Utils::from_dbrow<Dfs::DirRow>(row);
        if (dirRow.has_value()) {
            dirRow->actor_id = actorId;
            dirRows.push_back(dirRow.value());
        }
    }

    return dirRows;
}

std::expected<Dfs::DirRow, Dfs::DfsError> Dfs::Tables::ActorDirFile::get_dir_row(const ActorId     &actor_id,
                                                                                 const std::string &search_value,
                                                                                 const std::string &field) {
    auto db = get_actor_dir_file(actor_id);
    if (!db.is_open()) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    auto rows = db.select(fmt::format("SELECT * FROM {} WHERE {} = '{}';", TableName, field, search_value));
    if (rows.empty()) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    auto &row    = rows[0];
    auto  dirRow = Utils::from_dbrow<Dfs::DirRow>(row);

    if (!dirRow.has_value()) {
        return std::unexpected(Dfs::DfsError::DirValueNotExists);
    }

    dirRow->actor_id = actor_id;
    return dirRow.value();
}

bool Dfs::Tables::ActorDirFile::add_dir_row(const ActorId &actor_id, DirRow &dir_row) {
    auto dir_file = get_actor_dir_file(actor_id);

    if (!dir_file.is_open()) {
        return false;
    }

    auto current_ms   = Utils::current_date_ms();
    auto prev_file_id = DfsT::ActorDirFile::getLastFileId(dir_file);

    dir_row.created       = current_ms;
    dir_row.last_modified = current_ms;
    dir_row.prev_file_id  = prev_file_id;

    auto dirRowDb = Utils::to_dbrow(dir_row);
    bool res      = dir_file.replace(Dfs::Tables::ActorDirFile::TableName, dirRowDb);

    return res;
}

bool Dfs::Tables::ActorDirFile::add_dir_rows(const ActorId &actor_id, const std::vector<DirRow> &dir_rows) {
    auto dir_file = get_actor_dir_file(actor_id);

    if (!dir_file.is_open()) {
        return false;
    }

    for (auto &dir_row : dir_rows) {
        if (dir_row.hash.empty()) {
            continue;
        }

        auto dir_row_db = Utils::to_dbrow(dir_row);
        dir_file.replace(Dfs::Tables::ActorDirFile::TableName, dir_row_db);
    }

    return true;
}

std::filesystem::path Dfs::Path::filePath(const ActorId &actor_id, const std::string &file_id) {
    return DfsB::fsActrRoot + Utils::platformDelimeter() + actor_id.to_string() + Utils::platformDelimeter()
           + file_id;
}

std::expected<FsPath, FsError> Dfs::Path::file_path(const ActorId &actor_id, const std::string &file_id) {
    auto path = fmt::format("{}/{}/{}", DfsB::fsActrRoot, actor_id, file_id);
    return FsPath::create(path);
}

std::filesystem::path Dfs::Path::actorPath(const ActorId &actorId) {
    return DfsB::fsActrRoot + Utils::platformDelimeter() + actorId.to_string();
}

int Dfs::Tables::ActorDirFile::totalFileSize(const ActorId &actorId) {
    auto db = get_actor_dir_file(actorId);
    if (!db.is_open()) {
        eFatal("Database {} error", db.file());
        return 0;
    }

    auto count = db.count(TableName);
    if (count == 0)
        return 0;

    auto row = db.select(fmt::format("SELECT SUM(size) from {}", TableName)).at(0);
    return std::stoi(row["SUM(size)"]);
}

std::uint64_t Dfs::Tables::ActorDirFile::dataAmountStoredSize(const ActorId     &actorId,
                                                              const std::string &storjName) {
    DbConnector db(storjDbPath(actorId, storjName).string());
    db.open();
    if (!db.is_open()) {
        eFatal("Database error {}", db.file());
        return 0;
    }

    auto count = db.select(fmt::format("SELECT COUNT(size) from {}", DfsF::TableNameFragments))[0];
    if (std::stoi(count["COUNT(size)"]) == 0) {
        return 0;
    }

    auto  rows = db.select(fmt::format("SELECT SUM(size) from {}", DfsF::TableNameFragments));
    auto &row  = rows[0];

    return std::stoull(row["SUM(size)"]);
}

bool Dfs::Tables::ActorDirFile::update_file_metadata(const ActorId &actorId, DirRow &dirRow) {
    auto db = get_actor_dir_file(actorId);
    if (!db.is_open()) {
        eFatal("Database error {}", db.file());
        return 0;
    }

    std::string query =
        fmt::format("UPDATE {} SET hash = '{}', size = '{}', last_modified = '{}' WHERE file_id = '{}'",
                    TableName,
                    dirRow.hash,
                    dirRow.size,
                    dirRow.last_modified,
                    dirRow.file_id);
    return db.update(query);
}

std::optional<Dfs::CollectionTemplate> Dfs::Tables::ActorDirFile::get_collection_template_file_id(
    const ActorId &actor_id,
    std::string    file_id) {
    auto path = Path::file_path(actor_id, file_id);
    if (!path.has_value()) {
        eLog("Invalid path");
        return std::nullopt;
    }

    auto content = Utils::read_file_content(path.value());
    if (!content.has_value()) {
        return std::nullopt;
    }

    eLog("Read {} bytes", content->size());
    auto collection_template_result = Json::deserialize<Dfs::CollectionTemplate>(content.value());
    if (!collection_template_result.has_value()) {
        return std::nullopt;
    }

    auto collection_template = collection_template_result.value();
    collection_template.set_actor_file(actor_id, file_id);

    return collection_template;
}

std::optional<Dfs::CollectionTemplate> Dfs::Tables::ActorDirFile::get_collection_template_name(
    const ActorId     &actor_id,
    const std::string &template_name) {
    auto dir_row_exp = get_dir_row(actor_id, template_name, "name");
    if (!dir_row_exp.has_value()) {
        return std::nullopt;
    }
    auto dir_row = dir_row_exp.value();

    if (dir_row.folder != ":templates") {
        return std::nullopt;
    }

    auto collection_template = get_collection_template_file_id(actor_id, dir_row.file_id);

    return collection_template;
}

namespace magic {
    std::string custom_magic<Dfs::FileId>::read(const Dfs::FileId &value) {
        return value.value();
    }

    Dfs::FileId custom_magic<Dfs::FileId>::write(const std::string &value) {
        return Dfs::FileId::create(value).value();
    }
} // namespace magic
