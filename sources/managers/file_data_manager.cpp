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

#include "managers/file_data_manager.h"
#include "chain/actor.h"
#include "dfs/dfs_utils.h"

namespace {
    boost::json::object to_json(const FileData &file) {
        return {
            { name_file, file.nameFile },
            { path_file, file.pathFile },
            { status, static_cast<std::int64_t>(file.status) },
        };
    }
} // namespace

FileDataManager::FileDataManager() {
    updateAllTree();
}

boost::json::array FileDataManager::get_file_tree(ActorId actorId, bool shouldUpdateList) {
    boost::json::array array;

    if (actorId.is_zero() && savedActorId.is_zero()) {
        eWarning("ActorId and saved ActorId are empty");
        return array;
    }

    if (actorId.is_zero()) {
        actorId = savedActorId;
    }

    if (shouldUpdateList || files.empty())
        files = updateFileList(actorId);

    if (!shouldUpdateList) {
        files = cachedData[actorId];
    }

    for (const auto &file : files) {
        array.push_back(to_json(file));
    }
    return array;
}

std::vector<FileData> FileDataManager::updateFileList(const ActorId &actorId) {
    const auto            pathToActorFolder = Dfs::Path::actorPath(actorId);
    std::vector<FileData> fileStructs;

    // auto dir_rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(actorId);
    // if (!dir_rows.has_value()) {
    //     return fileStructs;
    // }

    // // directory_iterator
    // for (const auto &dir : dir_rows.value()) {
    //     const auto nameFile = dir.file_id;
    //     /* if (nameFile == DfsB::fsMapName || nameFile.extension() == DfsF::Extension)
    //         continue;
    //     */

    //     FileStatus status = FileStatus::None;
    //     switch (dir.state) {
    //     case Dfs::FileState::Ready:
    //         status = FileStatus::Downloaded;
    //         break;
    //     case Dfs::FileState::Partial:
    //         status = FileStatus::Loading;
    //         break;
    //     case Dfs::FileState::Known:
    //         status = FileStatus::NotLoaded;
    //         break;
    //     default:
    //         status = FileStatus::NotLoaded;
    //         break;
    //     }

    //     FileData fileStruct =
    //         FileData { .nameFile = dir.file_id, .pathFile = dir.visual_path(), .status = status };
    //     fileStructs.emplace_back(fileStruct);
    // }

    // // save to cache
    // cachedData[actorId] = fileStructs;

    return fileStructs;
}

const std::map<ActorId, std::vector<FileData>> &FileDataManager::getCachedData() const {
    return cachedData;
}

bool FileDataManager::update_status(std::string_view nameFile, FileStatus newStatus) {
    const auto fileData = std::find_if(files.begin(), files.end(), [&nameFile](const FileData &fileStruct) {
        return nameFile == fileStruct.nameFile;
    });

    if (fileData != files.end()) {
        fileData->status = newStatus;
        status_changed_event_.publish(*fileData);
        return true;
    }
    return false;
}

boost::json::object FileDataManager::get_file_data(std::string_view nameFile, bool shouldUpdateList) {
    if (shouldUpdateList)
        files = updateFileList(savedActorId);

    const auto fileData = std::find_if(files.begin(), files.end(), [&nameFile](const FileData &fileStruct) {
        return nameFile == fileStruct.nameFile;
    });

    return fileData == files.end() ? boost::json::object {} : to_json(*fileData);
}

boost::json::array FileDataManager::get_files_by_status(FileStatus fileStatus, bool shouldUpdateList) {
    if (shouldUpdateList)
        files = updateFileList(savedActorId);

    boost::json::array array;

    for (const auto &file : files) {
        if (file.status != fileStatus)
            continue;

        array.push_back(to_json(file));
    }
    return array;
}

void FileDataManager::setActorId(const ActorId &actorId) {
    savedActorId = actorId;
}

void FileDataManager::updateAllTree() {
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(DfsB::DFS_FOLDER, error), end; !error && iterator != end;
         iterator.increment(error)) {
        const auto &entry = *iterator;
        if (entry.is_directory()) {
            const auto actorId = ActorId(entry.path().filename().string());
            updateFileList(actorId);
        }
    }
}

ExtraChain::Core::Event<FileData> &FileDataManager::status_changed_event() noexcept {
    return status_changed_event_;
}
