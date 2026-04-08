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

#ifndef ISOCKETSERVICE_H
#define ISOCKETSERVICE_H

#include "encryption/key_private.h"
#include "encryption/key_public.h"
#include "utils/exc_utils.h"

#include <functional>
#include <memory>
#include <mutex>
#include <queue>

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
    using Ptr = std::shared_ptr<SocketService>;

    struct SocketPair {
        std::string ip;
        std::string identifier;

        bool operator==(const SocketPair &) const = default;

        bool operator<(const SocketPair &other) const {
            if (ip != other.ip) {
                return ip < other.ip;
            }

            return identifier < other.identifier;
        }
    };

    struct HandshakeMessage {
        std::string          network_id;
        std::string          version;
        std::string          identifier;
        int                  socket_type = 0;
        std::string          your_ip;
        std::set<SocketPair> connections;
        bool                 is_available = false;
        bool                 is_constant  = false;
        SocketMode           socket_mode;
        DfsMode              dfs_mode;
    };

    enum class Priority {
        Low,
        Normal,
        High
    };

    explicit SocketService(ExtraChainNode *node);
    virtual ~SocketService() = default;

    const std::string        &identifier() const;
    virtual std::string       protocol_string() const = 0;
    virtual Network::Protocol protocol() const        = 0;
    virtual bool              is_active() const       = 0;
    virtual uint16_t          port() const            = 0;
    virtual uint16_t          server_port() const     = 0;
    const std::string        &ip() const;
    DfsMode                   dfs_mode_socket() const;
    int64_t                   bytes_compressed() const;
    int64_t                   bytes_outgoing() const;
    int64_t                   bytes_incoming() const;
    bool                      is_constant() const;
    void                      set_constant(bool isConstant);
    SocketMode                mode() const { return mode_; }
    SocketDirection           direction() const { return direction_; }
    void                      set_direction(SocketDirection dir) { direction_ = dir; }
    std::uint64_t             timestamp() const;

    virtual void flush()                                                                          = 0;
    virtual void send_message(const std::vector<uint8_t> &data, Priority priority = Priority::High) = 0;

    bool is_closed();
    virtual void close_connection();
    long queue_size();
    virtual int64_t pending_bytes() const { return 0; }

    std::function<void(Ptr)> on_disconnected;
    std::function<void(Ptr, Network::SocketServiceError, const std::string &, const std::string &)> on_error;
    std::function<void(Ptr)> on_activated;
    std::function<void(Ptr, const std::set<SocketPair> &)> on_share_connections;

protected:
    bool                 check_first_message(const HandshakeMessage &msg);
    std::vector<uint8_t> generate_first_message();
    std::vector<uint8_t> prepare_send_message(const std::vector<uint8_t> &message);
    std::vector<uint8_t> prepare_receive_message(const std::vector<uint8_t> &message);

    ExtraChainNode *node_ = nullptr;
    std::string     identifier_;
    std::string     ip_;
    uint16_t        port_             = 0;
    bool            activated_        = false;
    bool            is_disconnected_  = false;
    int64_t         bytes_incoming_   = 0;
    int64_t         bytes_outgoing_   = 0;
    int64_t         bytes_compressed_ = 0;
    std::atomic_bool is_constant_     = false;
    std::uint64_t    timestamp_       = 0;
    SocketMode       mode_            = SocketMode::Full;
    SocketDirection  direction_       = SocketDirection::Outgoing;
    DfsMode          dfs_mode_socket_;

    std::mutex                       queue_mutex_;
    std::queue<std::vector<uint8_t>> high_queue_;
    std::queue<std::vector<uint8_t>> normal_queue_;
    std::queue<std::vector<uint8_t>> low_queue_;

    static constexpr int64_t MAX_BUFFER_SIZE = 10 * 1024 * 1024;
    bool                     waiting_buffer_space_ = false;

    KeyPrivate priv_   = KeyPrivate();
    KeyPublic  pub_    = KeyPublic();
    bool       is_pub_ = false;
    bool       closed_ = false;
};

BOOST_DESCRIBE_STRUCT(
    SocketService::HandshakeMessage,
    (),
    (network_id, version, identifier, socket_type, your_ip, connections, is_available, is_constant, socket_mode, dfs_mode))

BOOST_DESCRIBE_STRUCT(SocketService::SocketPair, (), (ip, identifier))

#endif // ISOCKETSERVICE_H
