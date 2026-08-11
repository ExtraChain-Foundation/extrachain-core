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

#include <cstddef>
#include <optional>
#include <string>

#include <boost/describe.hpp>

enum class DagMode {
    Full,
    Light
};

enum class DfsMode {
    Full,
    Light,
    Selective
};

enum class Force {
    None,
    Active
};

inline constexpr int CURRENT_DAG_VERSION = 100;
inline constexpr int CURRENT_DFS_VERSION = 100;

enum class ChainIndexMode {
    Disabled,
    Enabled
};

struct ExtraChainSettings {
    std::optional<std::string>    first_node;
    std::optional<DagMode>        dag_mode;
    std::optional<DfsMode>        dfs_mode;
    std::optional<std::string>    node_identifier;
    std::optional<int>            dag_version;
    std::optional<int>            dfs_version;
    std::optional<ChainIndexMode> chain_index_mode;
};

BOOST_DESCRIBE_STRUCT(
    ExtraChainSettings,
    (),
    (first_node, dag_mode, dfs_mode, node_identifier, dag_version, dfs_version, chain_index_mode))

namespace Network {

    inline bool        isStartedServer = true;
    inline bool        networkDebug    = false;
    inline std::size_t maxConnections =
#if defined(IS_APP_UI_CLIENT) && (defined(__ANDROID__) || defined(EXTRACHAIN_IOS))
        4;
#elif defined(IS_APP_UI_CLIENT)
        5;
#else
        1000;
#endif

    enum class Protocol {
        Undefined = 0,
        Udp       = 1,
        WebSocket = 2
    };

    enum class SocketServiceError {
        Unknown,
        VersionTooOld,
        VersionTooNew,
        IncompatibleNetwork,
        IncompatibleIdentifier,
        DuplicateIdentifier,
        IncorrectPublicKey,
        IncorrectFirstMessage,
        MaxConnections,
        PeerUnavailable,
        EmptyMessage,
        IncorrectMessage,
        CantSend,
        PhysicalKill,
        IncorrectHandshake,
        PongLost,
        Secs10Inactive
    };

} // namespace Network
