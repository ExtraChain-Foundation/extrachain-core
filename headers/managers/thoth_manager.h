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

#include "chain/actor_id.h"
#include "dfs/dfs_utils.h"

struct ThothData {
    std::string   id;
    std::uint64_t timestamp = 0;
    ActorId       actor;
    ActorId       owner;
    std::string   file_id;
    std::string   os;
    std::string   token;
};
BOOST_DESCRIBE_STRUCT(ThothData, (), (id, timestamp, actor, owner, file_id, os, token))

struct ThothInfo {
    std::string os;
    std::string token;
};

class ExtraChainNode;

class ThothManager {
public:
    ThothManager(ExtraChainNode* node);

    // for network
    bool create_thoth_template();

    // for apps
    bool create_thoth_vector();
    bool read_all();
    void dfs_vector_add_check(const ActorId& owner_id, const std::string& file_id);
    void network_thoth_record(const ActorId&     owner_id,
                              const std::string& file_id,
                              const std::string& os,
                              const std::string& token);

    void start();
    void stop();

    // for users
    bool add_thoth_record(const ActorId& owner_id, const std::string& file_id);
    // bool remove_thoth_record(const ActorId& owner_id, const std::st

private:
    ExtraChainNode* node;

    bool          enabled_ = false;
    std::uint16_t port_    = 5425;

    ActorId                            owner_id_;
    std::string                        file_id_;
    std::map<Dfs::FileLink, ThothInfo> infos_;
};
