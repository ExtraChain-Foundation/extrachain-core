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

#include <optional>
#include <string>

#include "utils/exc_utils.h"

/**
 * Snapshot of what we know about a peer after handshake.
 * Populated once in SocketService::check_first_message(), then consumed by
 * send and receive handlers to pick a compatible wire format or feature set.
 *
 * Add fields here (plus matching predicates) as new protocol features land —
 * handlers should never branch on raw version numbers directly.
 */
struct PeerMeta {
    std::string        version;       // wire protocol version string, e.g. "0.25.0"
    std::optional<int> dag_version;   // storage schema; nullopt = pre-versioning peer
    std::optional<int> dfs_version;   // placeholder, filled once DFS adds the field
    DfsMode            dfs_mode = DfsMode::Full;
    // socket_mode lives on SocketService itself — not duplicated here to avoid
    // dragging the Qt-heavy isocket_service.h into every handler that reads PeerMeta.

    // Capability predicates — defined next to the data so handlers don't
    // encode version arithmetic in ad-hoc checks.

    // Peer runs a pre-decimal, pre-pack dag. Send numeric fields in legacy hex
    // form and expect legacy message layouts in return.
    bool is_legacy_dag() const {
        return !dag_version.has_value() || *dag_version < CURRENT_DAG_VERSION;
    }

    // Peer understands whole-pack transfer messages (Phase 13).
    bool supports_pack_sync() const {
        return dag_version.value_or(0) >= CURRENT_DAG_VERSION;
    }
};
