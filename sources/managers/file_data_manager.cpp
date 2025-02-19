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
#include "blockchain/actor.h"
#include "dfs/dfs_utils.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

FileDataManager::FileDataManager(QObject *parent)
    : QObject(parent) {
    updateAllTree();
}

QJsonDocument FileDataManager::getFileTree(ActorId actorId, const bool &shouldUpdateList) {
    QJsonDocument document;
    QJsonArray    array;

    if (actorId.is_zero() && savedActorId.is_zero()) {
        eWarning("ActorId and saved ActorId are empty");
        return document;
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
        QJsonObject object;
        object.insert(name_file, QString::fromStdString(file.nameFile));
        object.insert(path_file, QString::fromStdString(file.pathFile));
        object.insert(status, file.status);
        array.push_back(object);
    }
    document.setArray(array);

    return document;
}

std::vector<FileData> FileDataManager::updateFileList(const ActorId &actorId) {
    const auto            pathToActorFolder = Dfs::Path::actorPath(actorId);
    std::vector<FileData> fileStructs;

    auto dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows(actorId);
    if (!dir_rows.has_value()) {
        return fileStructs;
    }

    // directory_iterator
    for (const auto &dir : dir_rows.value()) {
        const auto nameFile = dir.file_id;
        /* if (nameFile == DfsB::fsMapName || nameFile.extension() == DfsF::Extension)
            continue;
        */

        FileStatus status = FileStatus::None;
        switch (dir.state) {
        case Dfs::FileState::Ready:
            status = FileStatus::Downloaded;
            break;
        case Dfs::FileState::Partial:
            status = FileStatus::Loading;
            break;
        case Dfs::FileState::Known:
            status = FileStatus::NotLoaded;
            break;
        default:
            status = FileStatus::NotLoaded;
            break;
        }

        FileData fileStruct =
            FileData { .nameFile = dir.file_id, .pathFile = dir.visual_path(), .status = status };
        fileStructs.emplace_back(fileStruct);
    }

    // save to cache
    cachedData[actorId] = fileStructs;

    return fileStructs;
}

const std::map<ActorId, std::vector<FileData>> &FileDataManager::getCachedData() const {
    return cachedData;
}

bool FileDataManager::updateStatusByNameStatus(const std::string &nameFile, const FileStatus &newStatus) {
    const auto fileData = std::find_if(files.begin(), files.end(), [&nameFile](const FileData &fileStruct) {
        return nameFile == fileStruct.nameFile;
    });

    if (fileData != files.end()) {
        fileData->status = newStatus;
        emit statusChanged(*fileData);
        return true;
    }
    return false;
}

QJsonObject FileDataManager::getFileDataByName(const std::string &nameFile, const bool &shouldUpdateList) {
    if (shouldUpdateList)
        files = updateFileList(savedActorId);

    const auto fileData = std::find_if(files.begin(), files.end(), [&nameFile](const FileData &fileStruct) {
        return nameFile == fileStruct.nameFile;
    });

    QJsonObject object;
    object.insert(name_file, QString::fromStdString(fileData->nameFile));
    object.insert(path_file, QString::fromStdString(fileData->pathFile));
    object.insert(status, fileData->status);
    return object;
}

QJsonDocument FileDataManager::getFilesTreeByStatus(const FileStatus &fileStatus, const bool &shouldUpdateList) {
    if (shouldUpdateList)
        files = updateFileList(savedActorId);

    QJsonDocument document;
    QJsonArray    array;

    for (const auto &file : files) {
        if (file.status != fileStatus)
            continue;

        QJsonObject object;
        object.insert(name_file, QString::fromStdString(file.nameFile));
        object.insert(path_file, QString::fromStdString(file.pathFile));
        object.insert(status, file.status);
        array.push_back(object);
    }
    document.setArray(array);
    return document;
}

void FileDataManager::setActorId(const ActorId &actorId) {
    savedActorId = actorId;
}

void FileDataManager::updateAllTree() {
    for (const auto &entry : std::filesystem::directory_iterator(DfsB::DFS_FOLDER)) {
        if (entry.is_directory()) {
            const auto actorId = ActorId(entry.path().filename().string());
            updateFileList(actorId);
        }
    }
}
