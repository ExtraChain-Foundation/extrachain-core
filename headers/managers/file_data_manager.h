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

#include "blockchain/actor.h"

#include <QObject>
#include <vector>

class QJsonDocument;
class QJsonObject;
class QJsonArray;

static const char *name_file = "name_file";
static const char *path_file = "path_file";
static const char *status    = "status";

enum FileStatus {
    None,
    NotLoaded,
    Loading,
    Downloaded
};

struct FileData {
    std::string nameFile = "";
    std::string pathFile = "";
    FileStatus  status   = FileStatus::None;
};

class FileDataManager : public QObject {
    Q_OBJECT

public:
    FileDataManager(QObject *parent = nullptr);
    QJsonDocument getFileTree(ActorId actorId = ActorId(), const bool &shouldUpdateList = false);
    bool          updateStatusByNameStatus(const std::string &nameFile, const FileStatus &newStatus);
    QJsonObject   getFileDataByName(const std::string &nameFile, const bool &shouldUpdateList = false);
    QJsonDocument getFilesTreeByStatus(const FileStatus &fileStatus, const bool &shouldUpdateList = false);
    void          setActorId(const ActorId &actorId);
    void          updateAllTree();

    const std::map<ActorId, std::vector<FileData>> &getCachedData() const;

signals:
    void structuraChanged(FileData fileStruct);
    void statusChanged(FileData fileStruct);

protected:
    std::vector<FileData> updateFileList(const ActorId &actorId);

private:
    std::vector<FileData>                    files;
    std::map<ActorId, std::vector<FileData>> cachedData;
    ActorId                                  savedActorId;
};
