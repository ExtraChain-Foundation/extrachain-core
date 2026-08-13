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

#include "chain/actor.h"
#include "runtime/event.h"

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <vector>

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

class FileDataManager {
public:
    FileDataManager();
    boost::json::array  get_file_tree(ActorId actor_id = ActorId(), bool update = false);
    bool                update_status(std::string_view name, FileStatus status);
    boost::json::object get_file_data(std::string_view name, bool update = false);
    boost::json::array  get_files_by_status(FileStatus status, bool update = false);
    void          setActorId(const ActorId &actorId);
    void          updateAllTree();

    const std::map<ActorId, std::vector<FileData>> &getCachedData() const;
    ExtraChain::Core::Event<FileData>              &status_changed_event() noexcept;

protected:
    std::vector<FileData> updateFileList(const ActorId &actorId);

private:
    std::vector<FileData>                    files;
    std::map<ActorId, std::vector<FileData>> cachedData;
    ActorId                                  savedActorId;
    ExtraChain::Core::Event<FileData>        status_changed_event_;
};
