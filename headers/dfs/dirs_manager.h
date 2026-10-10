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
#include <map>
#include <mutex>
#include <set>

#include "chain/actor_id.h"
#include "dfs/dfs_utils.h"
#include "dfs/catalog_sync.h"

namespace ExtraChain::Core {
    class ExtraChainNode;
}
class LoadManager;
class Responder;

enum class DirsError {
    FileSystemError,
    ParseError,
    DownloadManagerError
};

class DirsManager {
public:
    DirsManager(ExtraChain::Core::ExtraChainNode* node);
    ~DirsManager();

    void update_dirs(const ActorId& actor_id, std::uint64_t last_modified);

    void        stop();
    void        temp_sync_all(const std::string &identifier);
    void        temp_sync_actors(const std::string &identifier, const std::vector<ActorId> &actors);
    std::string request_catalog_rows(const Dfs::CatalogRowsRequest &request, const Responder &target);
    std::string request_catalog_digest(const std::vector<ActorId> &allowed, const Responder &target);
    void        network_request_catalog_rows(const Dfs::CatalogRowsRequest &request, const Responder &responder);
    void        network_response_dir_rows(std::string_view data, const Responder &responder);
    void        network_request_legacy_files(const std::vector<ActorId> &owners, const Responder &responder);

    std::shared_ptr<DbConnector> get_db_instance();

    // Content-based catalog sync (#75). Storage thread only.
    std::vector<Dfs::Packets::CatalogDigest> catalog_digests(const std::vector<ActorId>& only = {});
    void sync_digest(const std::string& identifier, const std::vector<ActorId>& allowed);
    void network_request_digest(const Dfs::Packets::CatalogDigestRequest& request, const Responder& responder);
    void network_response_digest(std::string_view data, const Responder &responder);
    // Whether the peer behind this connection identifier has answered a digest request.
    // Per peer, not a global counter: a node with mixed peers gets replies from the new
    // ones within the fallback window, and a global counter would hide the silent old one.
    bool digest_answered(const std::string &identifier);

private:
    void old_dfs_to_new_dfs_converter();
    void                 merge_catalog_rows(const std::vector<Dfs::DirRow> &rows, const Responder &responder);
    std::vector<ActorId> bounded_scope(const std::string &peer, const std::vector<ActorId> &owners);
    struct CatalogWork;
    std::shared_ptr<CatalogWork> work_;

    std::shared_ptr<DbConnector> db_;

    std::mutex            digest_mutex_;
    std::set<std::string>             digest_answered_;
    ExtraChain::Core::ExtraChainNode* node;
};
