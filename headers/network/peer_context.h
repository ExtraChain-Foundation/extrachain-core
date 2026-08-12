/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <tuple>

#include "chain/actor_id.h"
#include "core/types.h"

class SocketService;

struct PeerConnection {
    std::string ip;
    std::string identifier;

    bool operator==(const PeerConnection&) const = default;

    bool operator<(const PeerConnection& other) const {
        return std::tie(ip, identifier) < std::tie(other.ip, other.identifier);
    }
};

class PeerContext {
public:
    virtual ~PeerContext() = default;

    [[nodiscard]] virtual ActorId                  local_network_id() const                             = 0;
    virtual void                                   adopt_network_id(const ActorId& network_id)          = 0;
    [[nodiscard]] virtual std::string              local_node_identifier() const                        = 0;
    [[nodiscard]] virtual DfsMode                  local_dfs_mode() const                               = 0;
    [[nodiscard]] virtual bool                     has_active_duplicate(std::string_view     identifier,
                                                                        const SocketService* candidate) = 0;
    [[nodiscard]] virtual int                      active_peer_count() const                            = 0;
    [[nodiscard]] virtual int                      peer_limit() const                                   = 0;
    [[nodiscard]] virtual std::set<PeerConnection> shareable_peers(std::string_view remote_ip) const    = 0;
    virtual void peer_authenticated(std::string_view identifier, std::string_view public_ip)            = 0;
    [[nodiscard]] virtual std::uint16_t local_server_port() const                                       = 0;
    [[nodiscard]] virtual bool          peer_processing_enabled() const                                 = 0;
};
