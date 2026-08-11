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

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <boost/describe.hpp>

#include "core/types.h"
#include "encryption/key_private.h"
#include "encryption/key_public.h"
#include "extrachain_global.h"
#include "network/peer_meta.h"

class ExtraChainNode;

enum class SocketMode {
    Full,
    Light
};

enum class SocketDirection {
    Outgoing,
    Incoming
};

class EXTRACHAIN_EXPORT SocketService : public std::enable_shared_from_this<SocketService> {
public:
    using Data = std::vector<std::uint8_t>;
    using Ptr  = std::shared_ptr<SocketService>;

    struct SocketPair {
        std::string ip;
        std::string identifier;

        bool operator==(const SocketPair&) const = default;

        bool operator<(const SocketPair& other) const {
            return std::tie(ip, identifier) < std::tie(other.ip, other.identifier);
        }
    };

    struct HandshakeMessage {
        std::string                          network_id;
        std::string                          version;
        std::string                          identifier;
        int                                  socket_type = 0;
        std::string                          your_ip;
        std::set<SocketPair>                 connections;
        bool                                 is_available = false;
        bool                                 is_constant  = false;
        SocketMode                           socket_mode  = SocketMode::Full;
        DfsMode                              dfs_mode     = DfsMode::Full;
        std::optional<int>                   dag_version;
        std::optional<std::string>           node_version;
        std::optional<std::set<std::string>> capabilities;
    };

    enum class Priority {
        Low,
        Normal,
        High
    };

    explicit SocketService(ExtraChainNode* node);
    virtual ~SocketService() = default;

    SocketService(const SocketService&)            = delete;
    SocketService& operator=(const SocketService&) = delete;

    [[nodiscard]] const std::string&        identifier() const noexcept;
    [[nodiscard]] const std::string&        ip() const noexcept;
    [[nodiscard]] virtual std::string       protocol_string() const = 0;
    [[nodiscard]] virtual Network::Protocol protocol() const        = 0;
    [[nodiscard]] virtual bool              is_active() const       = 0;
    [[nodiscard]] virtual std::uint16_t     port() const            = 0;
    [[nodiscard]] virtual std::uint16_t     server_port() const     = 0;
    [[nodiscard]] DfsMode                   dfs_mode_socket() const noexcept;
    [[nodiscard]] std::int64_t              bytes_compressed() const noexcept;
    [[nodiscard]] std::int64_t              bytes_outgoing() const noexcept;
    [[nodiscard]] std::int64_t              bytes_incoming() const noexcept;
    [[nodiscard]] bool                      is_constant() const noexcept;
    [[nodiscard]] SocketMode                mode() const noexcept;
    [[nodiscard]] SocketDirection           direction() const noexcept;
    [[nodiscard]] std::uint64_t             timestamp() const noexcept;
    [[nodiscard]] const PeerMeta&           peer_meta() const noexcept;
    [[nodiscard]] bool                      is_closed() const noexcept;
    [[nodiscard]] std::size_t               queue_size() const;

    void set_constant(bool constant) noexcept;
    void set_direction(SocketDirection direction) noexcept;

    virtual void flush()                                                                              = 0;
    virtual void send_message(std::span<const std::uint8_t> data, Priority priority = Priority::High) = 0;
    void         send_message(std::string_view data, Priority priority = Priority::High) {
        send_message(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()),
                                                   data.size()),
                     priority);
    }
    virtual void                       close_connection();
    [[nodiscard]] virtual bool         wait_closed(std::chrono::milliseconds timeout);
    [[nodiscard]] virtual std::int64_t pending_bytes() const noexcept;

    std::function<void(Ptr)> on_disconnected;
    std::function<void(Ptr,
                       Network::SocketServiceError,
                       const std::string&,
                       const std::string&,
                       const std::string&,
                       SocketDirection)>
                                                                    on_error;
    std::function<void(Ptr)>                                        on_activated;
    std::function<void(Ptr, const std::set<SocketPair>&)>           on_share_connections;
    std::function<void(Ptr, std::string, std::string, std::string)> on_message;

protected:
    bool check_first_message(const HandshakeMessage& message);
    Data generate_first_message();
    Data prepare_send_message(std::span<const std::uint8_t> message);
    Data prepare_receive_message(std::span<const std::uint8_t> message);

    ExtraChainNode*           node_ = nullptr;
    std::string               identifier_;
    std::string               ip_;
    std::uint16_t             port_         = 0;
    std::atomic_bool          activated_    = false;
    std::atomic_bool          disconnected_ = false;
    std::atomic_bool          constant_     = false;
    std::atomic_bool          closed_       = false;
    std::atomic<std::int64_t> bytes_incoming_ { 0 };
    std::atomic<std::int64_t> bytes_outgoing_ { 0 };
    std::atomic<std::int64_t> bytes_compressed_ { 0 };
    std::uint64_t             timestamp_       = 0;
    SocketMode                mode_            = SocketMode::Full;
    SocketDirection           direction_       = SocketDirection::Outgoing;
    DfsMode                   dfs_mode_socket_ = DfsMode::Full;
    PeerMeta                  peer_meta_;

    mutable std::mutex        queue_mutex_;
    std::queue<Data>          high_queue_;
    std::queue<Data>          normal_queue_;
    std::queue<Data>          low_queue_;
    std::atomic<std::int64_t> queued_bytes_ { 0 };

    static constexpr std::int64_t MAX_BUFFER_SIZE       = 1024 * 1024;
    bool                          waiting_buffer_space_ = false;

    KeyPrivate private_key_;
    KeyPublic  public_key_;
    bool       public_key_received_ = false;
};

BOOST_DESCRIBE_STRUCT(SocketService::HandshakeMessage,
                      (),
                      (network_id,
                       version,
                       identifier,
                       socket_type,
                       your_ip,
                       connections,
                       is_available,
                       socket_mode,
                       dag_version,
                       node_version,
                       capabilities))

BOOST_DESCRIBE_STRUCT(SocketService::SocketPair, (), (ip, identifier))
