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

#include <string>
#include <expected>

#include <QObject>
#include "chain/actor_id.h"
#include "dfs/dfs_utils.h"
#include "network/network_manager.h"

class ExtraChainNode;
class LoadManager;

enum class DirsError {
    FileSystemError,
    ParseError,
    DownloadManagerError
};

class DirsManager : public QObject {
    Q_OBJECT
public:
    DirsManager(ExtraChainNode* node);
    ~DirsManager();

    void update_dirs(const ActorId& actor_id, std::uint64_t last_modified);

    void sync(const std::string& identifier);
    void network_request_sync(const Responder& responder);
    void network_response_sync(std::uint64_t max_last_modified, const Responder& responder);

    void send_from_last_modified(std::uint64_t last_modified, const Responder& responder);
    void network_response_from_last_modified(
        const std::vector<Dfs::Tables::DirsFile::DirsSpace::DirsRow>& dirs_rows,
        const Responder&                                              responder);

    void network_request_dir_rows(const Dfs::Tables::DirsFile::DirsSpace::DirsRow& dirs_row,
                                  const Responder&                                 responder);
    void network_response_dir_rows(const std::vector<std::pair<ActorId, std::vector<Dfs::DirRow>>> response_data,
                                   const Responder&                                                responder);

    // temp
    void temp_sync_all(const std::string& identifier);
    void network_request_all(const Responder& responder);

    std::shared_ptr<DbConnector> get_db_instance();

signals:
    void convertion_started();
    void convertion_finished();

private:
    void old_dfs_to_new_dfs_converter();

    std::shared_ptr<DbConnector> db_;
    ExtraChainNode*              node;
};
