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

#include "chain/actor.h"
#include "utils/fs_path.h"

std::string Dfs::DirRow::calculate_hash(const ActorId &owner_id) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);

    std::string owner_id_str = owner_id.to_string();
    blake3_hasher_update(&hasher, owner_id_str.data(), owner_id_str.size());

    std::string actor_id = this->actor_id.to_string();
    blake3_hasher_update(&hasher, actor_id.data(), actor_id.size());

    blake3_hasher_update(&hasher, this->file_id.data(), this->file_id.size());

    if (this->prev_file_id.has_value()) {
        const std::string &prev = this->prev_file_id.value();
        blake3_hasher_update(&hasher, prev.data(), prev.size());
    }

    blake3_hasher_update(&hasher, this->hash.data(), this->hash.size());

    if (this->folder.has_value()) {
        const std::string &folder = this->folder.value();
        blake3_hasher_update(&hasher, folder.data(), folder.size());
    }

    blake3_hasher_update(&hasher, this->name.data(), this->name.size());

    if (this->type != Dfs::FileType::Vector && this->type != Dfs::FileType::Dictionary) {
        auto size_str = std::to_string(this->size);
        blake3_hasher_update(&hasher, size_str.data(), size_str.size());
    }

    auto created_str = std::to_string(this->created);
    blake3_hasher_update(&hasher, created_str.data(), created_str.size());

    if (this->type != Dfs::FileType::Vector && this->type != Dfs::FileType::Dictionary) {
        auto last_modified_str = std::to_string(this->last_modified);
        blake3_hasher_update(&hasher, last_modified_str.data(), last_modified_str.size());
    }

    auto type_int = std::to_string(std::to_underlying(this->type));
    blake3_hasher_update(&hasher, type_int.data(), type_int.size());

    auto encryption_int = std::to_string(int(this->encryption));
    blake3_hasher_update(&hasher, encryption_int.data(), encryption_int.size());

    if (state == FileState::Removed) {
        blake3_hasher_update(&hasher, std::string("removed").data(), std::string("removed").size());
    }

    uint8_t output[BLAKE3_OUT_LEN];
    blake3_hasher_finalize(&hasher, output, BLAKE3_OUT_LEN);

    return fmt::format("{:02x}", fmt::join(std::span(output, BLAKE3_OUT_LEN), ""));
}

std::vector<DbRow> Dfs::Tables::DirsFile::ActorSpace::getFileDataByName(const std::shared_ptr<DbConnector> db,
                                                                        const ActorId &owner_id,
                                                                        std::string    name) {
    std::string query = fmt::format("SELECT * FROM {} WHERE owner_id = '{}' AND file_id = '{}'",
                                    TableNameActorsFiles,
                                    owner_id.to_string(),
                                    name);
    return db->select(query);
}

std::string Dfs::Tables::DirsFile::ActorSpace::read_last_file_id(const std::shared_ptr<DbConnector> db,
                                                                 const ActorId                     &owner_id) {
    if (!db->is_open()) {
        eFatal("Database {} not opened", db->file());
    }

    auto        result            = db->select(Dfs::Tables::last_file_id_query(owner_id));
    auto        prev_row_optional = result.empty() ? std::optional<DbRow> {} : result[0];
    std::string last_file_id      = prev_row_optional ? prev_row_optional->at("file_id") : "";

    if (last_file_id.empty()) {
        auto count_result = db->select(fmt::format(
            "WITH end_files AS ("
            "SELECT f1.file_id, COUNT(*) OVER() as cnt "
            "FROM {} f1 LEFT JOIN {} f2 ON f1.file_id = f2.prev_file_id "
            "WHERE f1.owner_id = '{}' AND f2.prev_file_id IS NULL"
            ") SELECT cnt FROM end_files LIMIT 1",
            TableNameActorsFiles,
            TableNameActorsFiles,
            owner_id));

        if (!count_result.empty()) {
            auto cnt = std::stoi(count_result[0].at("cnt"));
            if (cnt > 1) {
                eWarning("[DFS] read_last_file_id: owner {} has {} chain ends (expected 1)", owner_id, cnt);
            }
        }

        auto result =
            db->select(fmt::format("SELECT file_id FROM {} WHERE owner_id = '{}' ORDER by created DESC LIMIT 1",
                                   TableNameActorsFiles,
                                   owner_id));
        auto prevRowOpt = result.empty() ? std::optional<DbRow> {} : result[0];
        last_file_id    = prevRowOpt ? prevRowOpt->at("file_id") : "";

        if (!last_file_id.empty()) {
            eWarning("[DFS] read_last_file_id: owner {} - used fallback, got {}", owner_id, last_file_id);
        }
    }

    return last_file_id;
}

std::filesystem::path Dfs::Tables::DirsFile::ActorSpace::storjDbPath(const ActorId     &actorId,
                                                                     const std::string &storjName) {
    std::string path = DfsB::DFS_FOLDER + Utils::platformDelimeter() + actorId.to_string()
                       + Utils::platformDelimeter() + storjName;
    return path;
}

std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id,
    std::uint64_t                      last_modified,
    const std::string                 &post_query) {
    std::vector<Dfs::DirRow> dir_rows;
    auto db_rows = db->select(fmt::format("SELECT * FROM {} WHERE owner_id = '{}' AND last_modified >= {} {}",
                                          TableNameActorsFiles,
                                          owner_id.to_string(),
                                          last_modified,
                                          post_query));

    for (auto &row : db_rows) {
        auto dir_row = Utils::from_dbrow<Dfs::DirRow>(row);
        if (dir_row.has_value()) {
            dir_rows.push_back(dir_row.value());
        }
    }

    return dir_rows;
}

std::expected<std::unordered_map<std::string, Dfs::DirRow>, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::
    get_dir_rows_map(const std::shared_ptr<DbConnector> db, const ActorId &owner_id, std::uint64_t last_modified) {
    std::unordered_map<std::string, Dfs::DirRow> dir_rows;
    auto db_rows = db->select(fmt::format("SELECT * FROM {} WHERE owner_id = '{}' AND last_modified >= {}",
                                          TableNameActorsFiles,
                                          owner_id.to_string(),
                                          last_modified));

    for (auto &row : db_rows) {
        auto dir_row = Utils::from_dbrow<Dfs::DirRow>(row);
        if (dir_row.has_value()) {
            dir_rows[dir_row->file_id] = dir_row.value();
        }
    }

    return dir_rows;
}

std::expected<std::string, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::last_file_id(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id,
    const std::string                 &file_id) {
    auto        result       = db->select(Dfs::Tables::last_file_id_query(owner_id));
    auto        prev_file_id = result.empty() ? std::optional<DbRow> {} : result[0];
    std::string last_file_id = prev_file_id ? prev_file_id->at("file_id") : "";
    return last_file_id;
}

void Dfs::Tables::DirsFile::ActorSpace::update_file_state(const std::shared_ptr<DbConnector> db,
                                                          const ActorId                     &owner_id,
                                                          const std::string                  file_id,
                                                          FileState                          state) {
    db->update(fmt::format("UPDATE {} SET state = '{}' WHERE owner_id='{}' AND file_id = '{}'",
                           TableNameActorsFiles,
                           std::to_underlying(state),
                           owner_id.to_string(),
                           file_id));
}

void Dfs::Tables::DirsFile::ActorSpace::update_file_after_stored_remove(const std::shared_ptr<DbConnector> db,
                                                                        const ActorId     &owner_id,
                                                                        const std::string &file_id,
                                                                        const Signature   &sign,
                                                                        std::uint64_t      last_modified) {
    auto query = fmt::format(
        "UPDATE {} SET folder = NULL, name = '', hash = '', last_modified = '{}', size = 0, sign = "
        "'{}' WHERE owner_id = '{}' AND file_id = '{}'",
        TableNameActorsFiles,
        last_modified,
        Utils::to_base64(sign),
        owner_id.to_string(),
        file_id);
    db->update(query);

    eLog("update_file_after_stored_remove {}", query);
}

std::expected<Dfs::DirRow, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::get_dir_row(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id,
    const std::string                 &search_value,
    const std::string                 &field) {
    auto rows = db->select(fmt::format("SELECT * FROM {} WHERE owner_id = '{}' AND {} = '{}';",
                                       TableNameActorsFiles,
                                       owner_id.to_string(),
                                       field,
                                       search_value));
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

std::expected<Dfs::DirRow, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id,
    const std::string                 &folder,
    const std::string                 &name) {
    std::string query_folder = folder.empty() ? "" : fmt::format("folder = '{}' AND", folder);
    std::string query = fmt::format("SELECT * FROM {} WHERE owner_id = '{}' AND {} name = '{}' AND state != '{}';",
                                    TableNameActorsFiles,
                                    owner_id.to_string(),
                                    query_folder,
                                    name,
                                    std::to_underlying(FileState::Removed));

    auto rows = db->select(query);
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

std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::search_files_by_folder_and_name(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id,
    const std::string                 &folder,
    const std::string                 &name) {
    std::string query_folder = folder.empty() ? "" : fmt::format("folder = '{}' AND", folder);
    std::string query = fmt::format("SELECT * FROM {} WHERE owner_id = '{}' AND {} name = '{}' AND state != '{}';",
                                    TableNameActorsFiles,
                                    owner_id.to_string(),
                                    query_folder,
                                    name,
                                    std::to_underlying(FileState::Removed));

    auto rows = db->select(query);
    if (rows.empty()) {
        return std::unexpected(Dfs::DfsError::NotExists);
    }

    std::vector<Dfs::DirRow> result;
    result.reserve(rows.size());

    for (const auto &row : rows) {
        auto dirRow = Utils::from_dbrow<Dfs::DirRow>(row);
        if (dirRow.has_value()) {
            result.push_back(dirRow.value());
        }
    }

    if (result.empty()) {
        return std::unexpected(Dfs::DfsError::DirValueNotExists);
    }

    return result;
}

std::expected<Dfs::DirRow, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::search_file_by_hash(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id,
    const std::string                 &hash) {
    std::string query = fmt::format("SELECT * FROM {} WHERE owner_id = '{}' AND hash = '{}' AND state != '{}';",
                                    TableNameActorsFiles,
                                    owner_id.to_string(),
                                    hash,
                                    std::to_underlying(FileState::Removed));

    auto rows = db->select(query);
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

bool Dfs::Tables::DirsFile::ActorSpace::add_dir_row(const std::shared_ptr<DbConnector> db,
                                                    const ActorId                     &owner_id,
                                                    DirRow                            &dir_row,
                                                    const Actor<KeyPrivate>           &signer) {
    auto current_ms   = Utils::current_date_ms();
    auto prev_file_id = read_last_file_id(db, owner_id);

    // if (prev_file_id.empty()) {
    //     return false;
    // }

    if (dir_row.created == 0) {
        dir_row.created = current_ms;
    }

    if (dir_row.last_modified == 0) {
        dir_row.last_modified = current_ms;
    }

    dir_row.prev_file_id = prev_file_id;

    auto sign = signer.key().sign(dir_row.calculate_hash(owner_id));
    if (!sign.has_value()) {
        return false;
    }
    dir_row.sign = sign.value();

    auto dir_row_db = Utils::to_dbrow(dir_row);
    bool res        = db->replace(TableNameActorsFiles, dir_row_db);

    return res;
}

std::pair<bool, std::vector<Dfs::DirRow>> Dfs::Tables::DirsFile::ActorSpace::add_dir_rows(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &actor_id,
    const std::vector<DirRow>         &dir_rows) {
    std::vector<Dfs::DirRow> result_dir_row;
    result_dir_row.reserve(dir_rows.size());

    for (auto &dir_row : dir_rows) {
        if (dir_row.hash.empty()) {
            continue;
        }

        auto dir_row_db = Utils::to_dbrow(dir_row);
        // TODO: temp, because this function used only for loads
        if (dir_row.state != Dfs::FileState::Removed && dir_row.type != Dfs::FileType::Folder) {
            dir_row_db["state"] = std::to_string(std::to_underlying(Dfs::FileState::Known));
        }

        bool res = db->insert(TableNameActorsFiles, dir_row_db);

        if (res) {
            result_dir_row.emplace_back(dir_row);
        }
        // else
        //     eLog("ActorSpace::add_dir_rows failed: {} ; {}", dir_row.owner_id, dir_row.file_id);
    }

    return { true, result_dir_row };
}

std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::get_folders(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id) {
    std::string query = fmt::format("SELECT * FROM {} WHERE owner_id = '{}' AND type = '{}' AND state != '{}';",
                                    TableNameActorsFiles,
                                    owner_id.to_string(),
                                    std::to_underlying(Dfs::FileType::Folder),
                                    std::to_underlying(Dfs::FileState::Removed));

    auto                     db_rows = db->select(query);
    std::vector<Dfs::DirRow> folders;
    folders.reserve(db_rows.size());

    for (auto &row : db_rows) {
        auto dir_row = Utils::from_dbrow<Dfs::DirRow>(row);
        if (dir_row.has_value()) {
            folders.push_back(dir_row.value());
        }
    }

    return folders;
}

std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::get_folder_contents(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id,
    const std::string                 &folder_file_id) {
    std::string query = fmt::format("SELECT * FROM {} WHERE owner_id = '{}' AND folder = '{}' AND state != '{}';",
                                    TableNameActorsFiles,
                                    owner_id.to_string(),
                                    folder_file_id,
                                    std::to_underlying(Dfs::FileState::Removed));

    auto                     db_rows = db->select(query);
    std::vector<Dfs::DirRow> contents;
    contents.reserve(db_rows.size());

    for (auto &row : db_rows) {
        auto dir_row = Utils::from_dbrow<Dfs::DirRow>(row);
        if (dir_row.has_value()) {
            contents.push_back(dir_row.value());
        }
    }

    return contents;
}

std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::get_folder_path(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id,
    const std::string                 &folder_file_id) {
    std::vector<Dfs::DirRow> path;
    std::string              current_id = folder_file_id;
    std::set<std::string>    visited;

    while (!current_id.empty()) {
        if (visited.contains(current_id)) {
            return std::unexpected(Dfs::DfsError::FolderCycle);
        }
        visited.insert(current_id);

        auto dir_row = get_dir_row(db, owner_id, current_id, "file_id");
        if (!dir_row.has_value()) {
            break;
        }

        path.push_back(dir_row.value());

        if (!dir_row->folder.has_value() || dir_row->folder->empty() || dir_row->folder->front() == ':') {
            break;
        }
        current_id = dir_row->folder.value();
    }

    std::reverse(path.begin(), path.end());
    return path;
}

std::expected<bool, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::is_folder(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id,
    const std::string                 &file_id) {
    auto dir_row = get_dir_row(db, owner_id, file_id, "file_id");
    if (!dir_row.has_value()) {
        return std::unexpected(Dfs::DfsError::NotExists);
    }

    return dir_row->type == Dfs::FileType::Folder;
}

std::expected<bool, Dfs::DfsError> Dfs::Tables::DirsFile::ActorSpace::validate_folder_hierarchy(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &owner_id,
    const std::string                 &folder_file_id,
    const std::string                 &new_parent_id) {
    std::string           current_id = new_parent_id;
    std::set<std::string> visited;

    while (!current_id.empty()) {
        if (current_id == folder_file_id) {
            return false;
        }
        if (visited.contains(current_id)) {
            return std::unexpected(Dfs::DfsError::FolderCycle);
        }
        visited.insert(current_id);

        auto dir_row = get_dir_row(db, owner_id, current_id, "file_id");
        if (!dir_row.has_value()) {
            break;
        }

        if (!dir_row->folder.has_value() || dir_row->folder->empty() || dir_row->folder->front() == ':') {
            break;
        }
        current_id = dir_row->folder.value();
    }

    return true;
}

std::filesystem::path Dfs::Path::filePath(const ActorId &actor_id, const std::string &file_id) {
    return DfsB::DFS_FOLDER + Utils::platformDelimeter() + actor_id.to_string() + Utils::platformDelimeter()
           + file_id;
}

std::expected<FsPath, FsError> Dfs::Path::file_path(const ActorId &owner_id, const std::string &file_id) {
    if (file_id.size() != 64 && !Utils::is_hex_string_lower(file_id)) {
        return std::unexpected(FsError::InvalidPath);
    }

    auto path = fmt::format("{}/{}/{}", DfsB::DFS_FOLDER, owner_id, file_id);
    return FsPath::create(path);
}

std::filesystem::path Dfs::Path::actorPath(const ActorId &actorId) {
    return DfsB::DFS_FOLDER + Utils::platformDelimeter() + actorId.to_string();
}

std::size_t Dfs::Tables::DirsFile::ActorSpace::totalFileSize(const std::shared_ptr<DbConnector> db,
                                                             const ActorId                     &actorId) {
    auto count = db->count(TableNameActorsFiles);
    if (count == 0)
        return 0;

    auto row = db->select(fmt::format("SELECT SUM(size) from {}", TableNameActorsFiles)).at(0);
    return std::stoll(row["SUM(size)"]);
}

std::uint64_t Dfs::Tables::DirsFile::ActorSpace::dataAmountStoredSize(const std::shared_ptr<DbConnector> db,
                                                                      const ActorId                     &actorId,
                                                                      const std::string &storjName) {
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

std::pair<std::string, uint64_t> Dfs::Tables::DirsFile::ActorSpace::calculate_collection_hash_size(
    const ActorId     &owner_id,
    const std::string &file_id,
    const std::string &sort_field) {
    auto        dfs_path = Dfs::Path::file_path(owner_id, file_id);
    DbConnector db(dfs_path->native());
    db.open();
    auto res = db.hash_size(sort_field);
    db.close();
    return res;
}

bool Dfs::Tables::DirsFile::ActorSpace::update_file_metadata(const std::shared_ptr<DbConnector> db,
                                                             const ActorId                     &owner_id,
                                                             DirRow                            &dir_row,
                                                             bool                               with_sign) {
    std::string sign;
    if (with_sign) {
        sign = fmt::format(", sign = '{}'", Utils::to_base64(dir_row.sign));
    }

    std::string query = fmt::format(
        "UPDATE {} SET hash = '{}', size = '{}', last_modified = '{}'{} WHERE owner_id = '{}' AND file_id = '{}'",
        TableNameActorsFiles,
        dir_row.hash,
        dir_row.size,
        dir_row.last_modified,
        sign,
        dir_row.owner_id,
        dir_row.file_id);
    auto upd = db->update(query);
    if (!upd) {
        return false;
    }

    return true;
}

std::expected<std::vector<std::uint8_t>, Utils::ContentError> Dfs::Tables::DirsFile::ActorSpace::get_file_content(
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

std::optional<Dfs::CollectionTemplate> Dfs::Tables::DirsFile::ActorSpace::get_collection_template_file_id(
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

    // eLog("[Dfs] Read {} from collection template bytes", content->size());
    auto collection_template_result = Json::deserialize<Dfs::CollectionTemplate>(content.value());
    if (!collection_template_result.has_value()) {
        return std::nullopt;
    }

    auto collection_template = collection_template_result.value();
    collection_template.set_actor_file(actor_id, file_id);

    return collection_template;
}

std::optional<Dfs::CollectionTemplate> Dfs::Tables::DirsFile::ActorSpace::get_collection_template_name(
    const std::shared_ptr<DbConnector> db,
    const ActorId                     &actor_id,
    const std::string                 &template_name) {
    auto dir_row_exp = get_dir_row(db, actor_id, template_name, "name");
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

std::expected<std::shared_ptr<DbConnector>, Dfs::Tables::DirsFile::DirsSpace::DirsError> Dfs::Tables::DirsFile::
    DirsSpace::database() {
    std::shared_ptr<DbConnector> dirs_file = std::make_shared<DbConnector>(Dfs::Basic::dirsPath);
    if (!dirs_file->open()) {
        eCritical("[DirsFile] Can't open dirs file");
        return std::unexpected(DirsError::DirsNotOpen);
    }

    return dirs_file;
}

std::expected<std::shared_ptr<DbConnector>, Dfs::Tables::DirsFile::DirsSpace::DirsError> Dfs::Tables::DirsFile::
    DirsSpace::create_file() {
    // create basic dirs file
    auto db_res = database();
    if (!db_res.has_value()) {
        return std::unexpected(DirsError::DirsNotOpen);
    }
    auto db = db_res.value();

    if (!db->create_table(Dfs::Tables::DirsFile::CreateTableQueryDirs)) {
        return std::unexpected(DirsError::Unknown);
    }

    db->query(DfsT::DirsFile::CreateTableQueryActorsFiles);
    db->query(DfsT::DirsFile::CreateIndexActorsFiles1);
    db->query(DfsT::DirsFile::CreateIndexActorsFiles2);
    db->query(DfsT::DirsFile::CreateIndexActorsFiles3);
    db->query(DfsT::DirsFile::CreateIndexActorsFiles4);
    db->query(DfsT::DirsFile::CreateIndexActorsFiles5);

    return db;
}

std::expected<std::vector<Dfs::Tables::DirsFile::DirsSpace::DirsRow>, Dfs::Tables::DirsFile::DirsSpace::DirsError>
Dfs::Tables::DirsFile::DirsSpace::load_all(const std::shared_ptr<DbConnector> db) {
    return load_from_modified(db, 0);
}

std::expected<std::vector<Dfs::Tables::DirsFile::DirsSpace::DirsRow>, Dfs::Tables::DirsFile::DirsSpace::DirsError>
Dfs::Tables::DirsFile::DirsSpace::load_from_modified(const std::shared_ptr<DbConnector> db,
                                                     uint64_t                           last_modified) {
    auto                 query      = fmt::format("SELECT * FROM {} WHERE last_modified > {}",
                             Dfs::Tables::DirsFile::TableNameDirs,
                             last_modified);
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

bool Dfs::Tables::DirsFile::DirsSpace::insert(const std::shared_ptr<DbConnector> db, const DirsRow &dirs_row) {
    auto dbrow = Utils::to_dbrow(dirs_row);
    bool res   = db->replace(Dfs::Tables::DirsFile::TableNameDirs, dbrow);

    if (!res) {
        eWarning("[DirsManager] Can't update dirs file");
    }

    return res;
}

void Dfs::Tables::DirsFile::DirsSpace::insert_vector(const std::shared_ptr<DbConnector> db,
                                                     const std::vector<DirsRow>        &dirs_rows) {
    // begin transaction
    for (const auto &dirs_row : dirs_rows) {
        auto dbrow = Utils::to_dbrow(dirs_row);
        bool res   = db->insert(Dfs::Tables::DirsFile::TableNameDirs, dbrow);
    }
    // end transaction
}

std::expected<std::uint64_t, Dfs::Tables::DirsFile::DirsSpace::DirsError> Dfs::Tables::DirsFile::DirsSpace::
    max_last_modified(const std::shared_ptr<DbConnector> db) {
    auto query      = fmt::format("SELECT MAX(last_modified) FROM {}", Dfs::Tables::DirsFile::TableNameDirs);
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
        return std::unexpected(DirsError::NoRows);
    }
}

std::expected<uint64_t, Dfs::Tables::DirsFile::DirsSpace::DirsError> Dfs::Tables::DirsFile::DirsSpace::
    last_modified(const std::shared_ptr<DbConnector> db, const ActorId &actor_id) {
    auto query      = fmt::format("SELECT last_modified FROM {} WHERE actor_id = '{}'",
                             Dfs::Tables::DirsFile::TableNameDirs,
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
        return std::unexpected(DirsError::NoRows);
    }
}

void Dfs::Tables::DirsFile::DirsSpace::update_row(const std::shared_ptr<DbConnector> db,
                                                  const ActorId                     &actor_id,
                                                  std::uint64_t                      last_modified) {
    auto dirs_row = DirsRow { .actor_id = actor_id, .last_modified = last_modified };
    insert(db, dirs_row);
}

void Dfs::initialize_actor_folder(const ActorId &actor_id) {
    auto actor_folder = DfsB::DFS_FOLDER + Utils::platformDelimeter() + actor_id.to_string();

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
