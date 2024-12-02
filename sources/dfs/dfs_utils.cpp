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
    std::string query = fmt::format("SELECT * FROM {} WHERE fileId = '{}'", TableName, name);
    return db->select(query);
}

std::string Dfs::Tables::ActorDirFile::getLastFileId(DbConnector &db) {
    if (!db.is_open()) {
        eFatal("Database {} not opened", db.file());
    }

    auto        result       = db.select(Dfs::Tables::filesTableLast);
    auto        prevRowOpt   = result.empty() ? std::optional<DbRow> {} : result[0];
    std::string lastFileName = prevRowOpt ? prevRowOpt->at("fileId") : "";
    return lastFileName;
}

DbConnector Dfs::Tables::ActorDirFile::actorDbConnector(const ActorId &actorId) {
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
    auto db = actorDbConnector(actorId);
    if (!db.is_open()) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    std::vector<Dfs::DirRow> dirRows;
    auto                     actrDirData =
        db.select(fmt::format("SELECT * FROM {} WHERE last_modified >= {}", TableName, last_modified));

    for (auto &row : actrDirData) {
        auto dirRow = Utils::from_dbrow<Dfs::DirRow>(row);
        if (dirRow.has_value()) {
            dirRow->actorId = actorId;
            dirRows.push_back(dirRow.value());
        }
    }

    return dirRows;
}

std::expected<Dfs::DirRow, Dfs::DfsError> Dfs::Tables::ActorDirFile::get_dir_row(const ActorId     &actor_id,
                                                                                 const std::string &search_value,
                                                                                 const std::string &field) {
    auto db = actorDbConnector(actor_id);
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

    dirRow->actorId = actor_id;
    return dirRow.value();
}

bool Dfs::Tables::ActorDirFile::addDirRow(const ActorId &actorId, DirRow &dirRow) {
    auto dirFile = actorDbConnector(actorId);

    if (!dirFile.is_open()) {
        return false;
    }

    auto current_ms = Utils::current_date_ms();
    auto fileIdPrev = DfsT::ActorDirFile::getLastFileId(dirFile);

    dirRow.created       = current_ms;
    dirRow.last_modified = current_ms;
    dirRow.fileIdPrev    = fileIdPrev;

    auto dirRowDb = Utils::to_dbrow(dirRow);
    bool res      = dirFile.replace(Dfs::Tables::ActorDirFile::TableName, dirRowDb);

    return res;
}

bool Dfs::Tables::ActorDirFile::addDirRows(const ActorId &actorId, const std::vector<DirRow> &dirRows) {
    auto dirFile = actorDbConnector(actorId);

    if (!dirFile.is_open()) {
        return false;
    }

    for (auto &dirRow : dirRows) {
        if (dirRow.hash.empty()) {
            continue;
        }

        auto dirRowDb = Utils::to_dbrow(dirRow);
        dirFile.replace(Dfs::Tables::ActorDirFile::TableName, dirRowDb);
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
    auto db = actorDbConnector(actorId);
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
    auto db = actorDbConnector(actorId);
    if (!db.is_open()) {
        eFatal("Database error {}", db.file());
        return 0;
    }

    std::string query =
        fmt::format("UPDATE {} SET hash = '{}', size = '{}', last_modified = '{}' WHERE fileId = '{}'",
                    TableName,
                    dirRow.hash,
                    dirRow.size,
                    dirRow.last_modified,
                    dirRow.fileId);
    return db.update(query);
}

std::optional<Dfs::DfsTemplate> Dfs::Tables::ActorDirFile::get_dfs_template(const ActorId     &actor_id,
                                                                            const std::string &template_name) {
    auto dir_row_exp = get_dir_row(actor_id, template_name, "name");
    if (!dir_row_exp.has_value()) {
        return std::nullopt;
    }
    auto dir_row = dir_row_exp.value();

    if (dir_row.folder != ":templates") {
        return std::nullopt;
    }

    auto path = Path::file_path(actor_id, dir_row.fileId);
    if (!path.has_value()) {
        eLog("Invalid path");
        return std::nullopt;
    }

    auto content = Utils::read_file_content(path.value());
    if (!content.has_value()) {
        return std::nullopt;
    }

    eLog("Read {} bytes", content->size());
    auto dfs_template = Json::deserialize<Dfs::DfsTemplate>(content.value());
    if (!dfs_template.has_value()) {
        return std::nullopt;
    }

    return dfs_template.value();
}

namespace magic {
    std::string custom_magic<Dfs::FileId>::read(const Dfs::FileId &value) {
        return value.value();
    }

    Dfs::FileId custom_magic<Dfs::FileId>::write(const std::string &value) {
        return Dfs::FileId::create(value).value();
    }
} // namespace magic
