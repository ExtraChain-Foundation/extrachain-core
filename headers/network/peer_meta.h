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
#include <set>
#include <string>
#include <string_view>

inline constexpr std::string_view DAG_TX_BATCH_CAPABILITY     = "dag_tx_batch_v1";
inline constexpr std::string_view TOKEN_MIGRATION_CAPABILITY  = "contract_token_migration_v1";
inline constexpr std::string_view DAG_REPAIR_CAPABILITY       = "dag_repair_v1";
inline constexpr std::string_view SHADOW_CONSENSUS_CAPABILITY = "shadow_consensus_v3";

#include "core/types.h"

/**
 * Snapshot of what we know about a peer after handshake.
 * Populated once in SocketService::check_first_message(), then consumed by
 * send and receive handlers to pick a compatible wire format or feature set.
 *
 * Add fields here (plus matching predicates) as new protocol features land —
 * handlers should never branch on raw version numbers directly.
 */
struct PeerMeta {
    std::string                version;      // frozen 0.25.0 protocol-compat anchor
    std::optional<std::string> node_version; // real release version, e.g. "0.26.0"; nullopt = pre-0.26 peer
    std::optional<int>         dag_version;  // storage schema; nullopt = pre-versioning peer
    std::optional<int>         dfs_version;  // placeholder, filled once DFS adds the field
    std::set<std::string>      capabilities;
    DfsMode                    dfs_mode = DfsMode::Full;
    // socket_mode lives on SocketService itself — not duplicated here to avoid
    // dragging the Qt-heavy isocket_service.h into every handler that reads PeerMeta.

    // Capability predicates — defined next to the data so handlers don't
    // encode version arithmetic in ad-hoc checks.

    // TEMPORARY 0.26 legacy compat: pre-0.26 peer (no pack-sync/snapshots, hex
    // wire) drives the file-sync fallback. Always false once all nodes >= 0.26.
    bool is_legacy_dag() const {
        return !dag_version.has_value() || *dag_version < CURRENT_DAG_VERSION;
    }

    // Peer understands whole-pack transfer messages (Phase 13).
    bool supports_pack_sync() const {
        return dag_version.value_or(0) >= CURRENT_DAG_VERSION;
    }

    // Peer advertised a real release version (>= 0.26); pre-0.26 peers omit it.
    bool is_new_node() const {
        return node_version.has_value();
    }

    bool supports_dag_tx_batch() const {
        return capabilities.contains(std::string(DAG_TX_BATCH_CAPABILITY));
    }

    bool supports_token_migration() const {
        return capabilities.contains(std::string(TOKEN_MIGRATION_CAPABILITY));
    }

    bool supports_dag_repair() const {
        return capabilities.contains(std::string(DAG_REPAIR_CAPABILITY));
    }

    bool supports_shadow_consensus() const {
        return capabilities.contains(std::string(SHADOW_CONSENSUS_CAPABILITY));
    }
};
