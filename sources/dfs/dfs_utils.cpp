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

    auto        result       = db.select(Dfs::Tables::last_file_id_query);
    auto        prevRowOpt   = result.empty() ? std::optional<DbRow> {} : result[0];
    std::string lastFileName = prevRowOpt ? prevRowOpt->at("file_id") : "";
    return lastFileName;
}

DbConnector Dfs::Tables::ActorDirFile::get_actor_dir_file(const ActorId &owner_id) {
    auto path        = actorDbPath(owner_id);
    bool need_create = false;
    try {
        need_create = std::filesystem::file_size(path) == 0;
    } catch (std::exception &e) {
        need_create = true;
    }

    if (need_create) {
        Dfs::initialize_actor_folder(owner_id);
    }

    DbConnector db(path.string());
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
    const ActorId &owner_id,
    std::uint64_t  last_modified) {
    auto db = get_actor_dir_file(owner_id);
    if (!db.is_open()) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    std::vector<Dfs::DirRow> dirRows;
    auto                     actrDirData =
        db.select(fmt::format("SELECT * FROM {} WHERE last_modified >= {}", TableName, last_modified));

    for (auto &row : actrDirData) {
        auto dirRow = Utils::from_dbrow<Dfs::DirRow>(row);
        if (dirRow.has_value()) {
            dirRows.push_back(dirRow.value());
        }
    }

    return dirRows;
}

std::expected<std::string, Dfs::DfsError> Dfs::Tables::ActorDirFile::last_file_id(const ActorId     &owner_id,
                                                                                  const std::string &file_id) {
    auto db = get_actor_dir_file(owner_id);
    if (!db.is_open()) {
        eFatal("Database {} not opened", db.file());
    }

    auto        result       = db.select(Dfs::Tables::last_file_id_query);
    auto        prev_file_id = result.empty() ? std::optional<DbRow> {} : result[0];
    std::string last_file_id = prev_file_id ? prev_file_id->at("file_id") : "";
    return last_file_id;
}

void Dfs::Tables::ActorDirFile::update_file_state(const ActorId    &actor_id,
                                                  const std::string file_id,
                                                  FileState         state) {
    auto actrDirFile = get_actor_dir_file(actor_id);
    actrDirFile.update(fmt::format("UPDATE {} SET state = '{}' WHERE file_id = '{}'",
                                   TableName,
                                   std::to_underlying(state),
                                   file_id));
    actrDirFile.close();
}

std::expected<Dfs::DirRow, Dfs::DfsError> Dfs::Tables::ActorDirFile::get_dir_row(const ActorId     &owner_id,
                                                                                 const std::string &search_value,
                                                                                 const std::string &field) {
    auto db = get_actor_dir_file(owner_id);
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

    return dirRow.value();
}

std::expected<Dfs::DirRow, Dfs::DfsError> Dfs::Tables::ActorDirFile::search_file_by_folder_and_name(
    const ActorId     &owner_id,
    const std::string &folder,
    const std::string &name) {
    auto db = get_actor_dir_file(owner_id);
    if (!db.is_open()) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    std::string query_folder = folder.empty() ? "" : std::format("folder = '{}' AND", folder);
    std::string query        = fmt::format("SELECT * FROM {} WHERE {} name = '{}' AND state != '{}';",
                                    TableName,
                                    query_folder,
                                    name,
                                    std::to_underlying(FileState::Removed));

    auto rows = db.select(query);
    if (rows.empty()) {
        return std::unexpected(Dfs::DfsError::NotExists);
    }

    auto &row    = rows[0];
    auto  dirRow = Utils::from_dbrow<Dfs::DirRow>(row);

    if (!dirRow.has_value()) {
        return std::unexpected(Dfs::DfsError::DirValueNotExists);
    }

    return dirRow.value();
}

bool Dfs::Tables::ActorDirFile::add_dir_row(const ActorId           &owner_id,
                                            DirRow                  &dir_row,
                                            const Actor<KeyPrivate> &signer) {
    auto dir_file = get_actor_dir_file(owner_id);

    if (!dir_file.is_open()) {
        return false;
    }

    auto current_ms   = Utils::current_date_ms();
    auto prev_file_id = DfsT::ActorDirFile::getLastFileId(dir_file);

    dir_row.created       = current_ms;
    dir_row.last_modified = current_ms;
    dir_row.prev_file_id  = prev_file_id;

    auto sign = signer.key().sign(Utils::calculate_hash(dir_row));
    if (!sign.has_value()) {
        return false;
    }
    dir_row.sign = sign.value();

    auto dir_row_db = Utils::to_dbrow(dir_row);
    bool res        = dir_file.replace(Dfs::Tables::ActorDirFile::TableName, dir_row_db);

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
        // TODO: temp, because this function used only for loads
        dir_row_db["state"] = std::to_string(std::to_underlying(Dfs::FileState::Known));
        dir_file.insert(Dfs::Tables::ActorDirFile::TableName, dir_row_db);
    }

    return true;
}

std::filesystem::path Dfs::Path::filePath(const ActorId &actor_id, const std::string &file_id) {
    return DfsB::fsActrRoot + Utils::platformDelimeter() + actor_id.to_string() + Utils::platformDelimeter()
           + file_id;
}

std::expected<FsPath, FsError> Dfs::Path::file_path(const ActorId &owner_id, const std::string &file_id) {
    if (file_id.size() != 64 && !Utils::is_hex_string_lower(file_id)) {
        return std::unexpected(FsError::InvalidPath);
    }

    auto path = fmt::format("{}/{}/{}", DfsB::fsActrRoot, owner_id, file_id);
    return FsPath::create(path);
}

std::filesystem::path Dfs::Path::actorPath(const ActorId &actorId) {
    return DfsB::fsActrRoot + Utils::platformDelimeter() + actorId.to_string();
}

std::size_t Dfs::Tables::ActorDirFile::totalFileSize(const ActorId &actorId) {
    auto db = get_actor_dir_file(actorId);
    if (!db.is_open()) {
        eFatal("Database {} error", db.file());
        return 0;
    }

    auto count = db.count(TableName);
    if (count == 0)
        return 0;

    auto row = db.select(fmt::format("SELECT SUM(size) from {}", TableName)).at(0);
    return std::stoll(row["SUM(size)"]);
}

std::uint64_t Dfs::Tables::ActorDirFile::dataAmountStoredSize(const ActorId     &actorId,
                                                              const std::string &storjName) {
    DbConnector db(storjDbPath(actorId, storjName).string());
    db.open();
    if (!db.is_open()) {
        eFatal("Database error {}", db.file());
        return 0;
    }

    /*
    auto count = db.select(fmt::format("SELECT COUNT(size) from {}", DfsF::TableNameFragments))[0];
    if (std::stoi(count["COUNT(size)"]) == 0) {
        return 0;
    }

    auto  rows = db.select(fmt::format("SELECT SUM(size) from {}", DfsF::TableNameFragments));
    auto &row  = rows[0];

    return std::stoull(row["SUM(size)"]);
    */
    return 0;
}

std::pair<std::string, uint64_t> Dfs::Tables::ActorDirFile::calculate_collection_hash_size(
    const ActorId     &owner_id,
    const std::string &file_id) {
    auto        dfs_path = Dfs::Path::file_path(owner_id, file_id);
    DbConnector db(dfs_path->native());
    db.open();
    auto res = db.hash_size("id");
    db.close();
    return res;
}

bool Dfs::Tables::ActorDirFile::update_file_metadata(const ActorId &ownerr_id, DirRow &dir_row) {
    auto db = get_actor_dir_file(ownerr_id);
    if (!db.is_open()) {
        eFatal("Database error {}", db.file());
        return 0;
    }

    std::string query = fmt::
        format("UPDATE {} SET hash = '{}', size = '{}', last_modified = '{}', sign = '{}' WHERE file_id = '{}'",
               TableName,
               dir_row.hash,
               dir_row.size,
               dir_row.last_modified,
               dir_row.file_id,
               dir_row.sign);
    auto upd = db.update(query);
    if (!upd) {
        return false;
    }

    return true;
}

std::expected<std::vector<std::uint8_t>, Utils::ContentError> Dfs::Tables::ActorDirFile::get_file_content(
    const ActorId     &actor_id,
    const std::string &file_id) {
    auto path = Path::file_path(actor_id, file_id);

    if (!path.has_value()) {
        eLog("Invalid path");
        return std::unexpected(Utils::ContentError::InvalidFile);
    }

    auto content = Utils::read_file_content(path.value());
    if (!content.has_value()) {
        return std::unexpected(Utils::ContentError::InvalidFile);
    }

    if (content->empty()) {
        return std::unexpected(Utils::ContentError::InvalidFile);
    }

    eLog("[Dfs] Read {} bytes from actor {} and file {}", content->size(), actor_id, file_id);

    return content;
}

std::optional<Dfs::CollectionTemplate> Dfs::Tables::ActorDirFile::get_collection_template_file_id(
    const ActorId     &actor_id,
    const std::string &file_id) {
    auto path = Path::file_path(actor_id, file_id);
    if (!path.has_value()) {
        eLog("Invalid path");
        return std::nullopt;
    }

    auto content = Utils::read_file_content(path.value());
    if (!content.has_value()) {
        return std::nullopt;
    }

    eLog("[Dfs] Read {} from collection template bytes", content->size());
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

std::expected<DbConnector, Dfs::DirsFile::DirsError> Dfs::DirsFile::database() {
    DbConnector dirs_file(Dfs::Basic::dirsPath);
    if (!dirs_file.open()) {
        eCritical("[DirsFile] Can't open dirs file");
        return std::unexpected(DirsError::DirsNotOpen);
    }

    return dirs_file;
}

bool Dfs::DirsFile::create_file() {
    // create basic dirs file
    auto db = database();
    if (!db.has_value()) {
        return false;
    }
    if (!db->create_table(Dfs::Tables::DirsFile::CreateTableQuery)) {
        return false;
    }
    db->close();

    return true;
}

std::expected<std::vector<Dfs::DirsFile::DirsRow>, Dfs::DirsFile::DirsError> Dfs::DirsFile::load_all() {
    return load_from_modified(0);
}

std::expected<std::vector<Dfs::DirsFile::DirsRow>, Dfs::DirsFile::DirsError> Dfs::DirsFile::load_from_modified(
    uint64_t last_modified) {
    auto db = database();
    if (!db.has_value()) {
        return {};
    }

    auto query =
        fmt::format("SELECT * FROM {} WHERE last_modified > {}", Dfs::Tables::DirsFile::TableName, last_modified);
    auto                 all_dbrows = db->select(query);
    std::vector<DirsRow> dirs_rows;
    dirs_rows.reserve(all_dbrows.size());

    for (const auto &dbrow : all_dbrows) {
        auto dirs_row = Utils::from_dbrow<DirsRow>(dbrow);
        if (!dirs_row.has_value()) {
            continue;
        }
        dirs_rows.push_back(dirs_row.value());
    }

    return dirs_rows;
}

bool Dfs::DirsFile::insert(const DirsRow &dirs_row) {
    auto db = database();
    if (!db.has_value()) {
        return false;
    }

    auto dbrow = Utils::to_dbrow(dirs_row);
    bool res   = db->replace(Dfs::Tables::DirsFile::TableName, dbrow);

    if (!res) {
        eWarning("[DirsManager] Can't update dirs file");
    }

    return res;
}

void Dfs::DirsFile::insert_vector(const std::vector<DirsRow> &dirs_rows) {
    auto db = database();
    if (!db.has_value()) {
        return;
    }

    // begin transaction
    for (const auto &dirs_row : dirs_rows) {
        auto dbrow = Utils::to_dbrow(dirs_row);
        bool res   = db->insert(Dfs::Tables::DirsFile::TableName, dbrow);
    }
    // end transaction
}

std::expected<std::uint64_t, Dfs::DirsFile::DirsError> Dfs::DirsFile::max_last_modified() {
    auto db = database();
    if (!db.has_value()) {
        return std::unexpected(Dfs::DirsFile::DirsError::DirsNotOpen);
    }

    auto query      = fmt::format("SELECT MAX(last_modified) FROM {}", Dfs::Tables::DirsFile::TableName);
    auto all_dbrows = db->select(query);
    if (all_dbrows.empty()) {
        return 0;
    }
    if (all_dbrows.front().empty()) {
        return 0;
    }

    try {
        std::uint64_t max_last = std::stoull(all_dbrows.front().at("MAX(last_modified)"));
        return max_last;
    } catch (const std::exception &) {
        return std::unexpected(Dfs::DirsFile::DirsError::NoRows);
    }
}

std::expected<uint64_t, Dfs::DirsFile::DirsError> Dfs::DirsFile::last_modified(const ActorId &actor_id) {
    auto db = database();
    if (!db.has_value()) {
        return std::unexpected(Dfs::DirsFile::DirsError::DirsNotOpen);
    }

    auto query      = fmt::format("SELECT last_modified FROM {} WHERE actor_id = '{}'",
                             Dfs::Tables::DirsFile::TableName,
                             actor_id);
    auto all_dbrows = db->select(query);
    if (all_dbrows.empty()) {
        return 0;
    }
    if (all_dbrows.front().empty()) {
        return 0;
    }

    try {
        std::uint64_t max_last = std::stoull(all_dbrows.front().at("last_modified"));
        return max_last;
    } catch (const std::exception &) {
        return std::unexpected(Dfs::DirsFile::DirsError::NoRows);
    }
}

void Dfs::DirsFile::update_row(const ActorId &actor_id, std::uint64_t last_modified) {
    auto dirs_row = Dfs::DirsFile::DirsRow { .actor_id = actor_id, .last_modified = last_modified };
    Dfs::DirsFile::insert(dirs_row);
}

void Dfs::initialize_actor_folder(const ActorId &actor_id) {
    auto actor_folder = DfsB::fsActrRoot + Utils::platformDelimeter() + actor_id.to_string();

    auto folder = FsPath::create(actor_folder);
    if (!folder.has_value()) {
        return;
    }
    auto exists = folder->exists();
    auto size   = folder->file_size();
    if (size.has_value() && size.value() != 0) {
        return;
    }

    std::filesystem::create_directories(actor_folder);

    // create dir file
    auto        path     = Dfs::Tables::ActorDirFile::actorDbPath(actor_id);
    DbConnector dir_file = DbConnector(path);
    dir_file.open();
    dir_file.query(DfsT::ActorDirFile::CreateTableQuery);

    // requestDirData(actorId);
}

namespace magic {
    std::string custom_magic<Dfs::FileId>::read(const Dfs::FileId &value) {
        return value.value();
    }

    Dfs::FileId custom_magic<Dfs::FileId>::write(const std::string &value) {
        return Dfs::FileId::create(value).value();
    }
} // namespace magic
