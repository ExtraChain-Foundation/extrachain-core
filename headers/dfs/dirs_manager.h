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

#include <map>
#include <string>
#include <expected>
#include <QObject>
#include "dfs/dfs_utils.h"

class ExtraChainNode;
class DownloadManager;
using DirRow = Dfs::DirRow;

enum class DirsError {
    FileSystemError,
    ParseError,
    DownloadManagerError
};

class DirsManager {
public:
    DirsManager(ExtraChainNode* node);

    void initialize_actor_folder(const ActorId& actorId);
    void update_dirs(const ActorId& actor_id, std::uint64_t last_modified);

    // Load initial state from .dirs and .dir files
    std::expected<void, DirsError> load_initial_state();

    // Get file info by id
    [[nodiscard]] std::optional<DirRow> get_file_info(const std::string& file_id) const;

    // Slot for updating from .dirs changes
    void on_dirs_updated();

private:
    ExtraChainNode* node;

    std::map<std::string, DirRow> files;

    // Internal helpers
    std::expected<void, DirsError> load_dirs_file();
    std::expected<void, DirsError> load_dir_file(const std::string& dir_id);
    void                           process_new_files();
};
