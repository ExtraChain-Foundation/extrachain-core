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

#include "dfs/dirs_manager.h"

#include "managers/extrachain_node.h"
#include "dfs/download_manager.h"
#include "utils/exc_logs.h"

// тебе нужно будет добавить:
// #include "blockchain/dirs.h" - для работы с .dirs файлом
// #include "network/dfs_sync_messages.h"

DirsManager::DirsManager(ExtraChainNode* node)
    : node(node) {
    // create dfs folder
    std::filesystem::create_directories(DfsB::fsActrRoot);

    // basic creation of dirs file
    bool dirs_result = Dfs::DirsFile::create_file();
    if (!dirs_result) {
        eFatal("[DirsManager] Can't create basic .dirs file");
    }
}

void DirsManager::initialize_actor_folder(const ActorId& actorId) {
    std::string path_delim = Utils::platformDelimeter();
    std::filesystem::create_directories(DfsB::fsActrRoot + path_delim + actorId.to_string());
    DbConnector dir_file = DfsT::ActorDirFile::get_actor_dir_file(actorId);
    dir_file.query(DfsT::ActorDirFile::CreateTableQuery);
    // requestDirData(actorId);
}

void DirsManager::update_dirs(const ActorId& actor_id, uint64_t last_modified) {
    auto max_last_modified = Dfs::DirsFile::max_last_modified();
    if (!max_last_modified.has_value()) {
        return;
    }
    if (last_modified <= max_last_modified.value()) {
        return;
    }

    auto dirs_row = Dfs::DirsFile::DirsRow { .actor_id = actor_id, .last_modified = last_modified };
    Dfs::DirsFile::insert(dirs_row);
}

std::expected<void, DirsError> DirsManager::load_initial_state() {
    // Загружаем .dirs
    auto dirs_result = load_dirs_file();
    if (!dirs_result.has_value()) {
        return std::unexpected(dirs_result.error());
    }

    // После загрузки проверяем, что надо докачать
    process_new_files();
    return {};
}

std::optional<DirRow> DirsManager::get_file_info(const std::string& file_id) const {
    auto it = files.find(file_id);
    if (it != files.end()) {
        return it->second;
    }
    return std::nullopt;
}

void DirsManager::on_dirs_updated() {
    eLog("DirsManager: .dirs file updated");

    auto result = load_dirs_file();
    if (!result.has_value()) {
        eCritical("Failed to load .dirs file after update");
        return;
    }

    process_new_files();
}

std::expected<void, DirsError> DirsManager::load_dirs_file() {
    /*
    Тебе нужно будет добавить:
    1. Чтение .dirs файла
    2. Парсинг его содержимого
    3. Для каждой записи вызов load_dir_file()
    */
    return {};
}

std::expected<void, DirsError> DirsManager::load_dir_file(const std::string& dir_id) {
    /*
    Тебе нужно будет добавить:
    1. Чтение .dir файла по dir_id
    2. Парсинг его содержимого в DirRow
    3. Обновление files map
    */
    return {};
}

void DirsManager::process_new_files() {
    // for (const auto& [file_id, row] : files) {
    //     if (row.state != Dfs::FileState::Ready) {
    //         download_manager->add_to_queue(row.actor_id, file_id, row.state);
    //     }
    // }
}
