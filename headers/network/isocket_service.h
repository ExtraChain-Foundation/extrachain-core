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
#include "network/peer_meta.h"
#include "utils/exc_utils.h"

#include <atomic>
#include <queue>
#include <QMutex>

class ExtraChainNode;

enum class SocketMode {
    Full,
    Light
};

enum class SocketDirection {
    Outgoing, // I connected to peer
    Incoming  // Peer connected to me
};

class EXTRACHAIN_EXPORT SocketService : public QObject {
    Q_OBJECT

public:
    struct SocketPair {
        std::string ip;
        std::string identifier;

        bool operator==(const SocketPair &) const = default;

        bool operator<(const SocketPair &other) const {
            if (ip != other.ip)
                return ip < other.ip;
            return identifier < other.identifier;
        }
    };

    struct HandshakeMessage {
        std::string          network_id;
        std::string          version; // frozen 0.25.0 protocol-compat anchor for the strict version check
        std::string          identifier;
        int                  socket_type = 0; // compability
        std::string          your_ip;
        std::set<SocketPair> connections;
        bool                 is_available = false;
        bool                 is_constant  = false;
        SocketMode           socket_mode  = SocketMode::Full;
        DfsMode              dfs_mode     = DfsMode::Full;
        // Storage schema version. Optional so legacy peers (who don't send it)
        // parse cleanly — JSON serialization drops absent optionals entirely.
        // Present + >= 100 = peer understands decimal wire format and pack sync.
        std::optional<int> dag_version;
        // Real release version (e.g. "0.26.0"). Optional so pre-0.26 peers parse
        // cleanly; not version-checked, only a signal that the peer is >= 0.26.
        std::optional<std::string> node_version;
    };

    enum class Priority {
        Low,
        Normal,
        High
    };

    explicit SocketService(ExtraChainNode *node, QObject *parent = nullptr);
    const QString            &identifier() const;
    virtual QString           protocol_string() const = 0;
    virtual Network::Protocol protocol() const        = 0;
    virtual bool              is_active() const       = 0;
    virtual quint16           port() const            = 0;
    virtual quint16           server_port() const     = 0;
    const QString            &ip() const;
    DfsMode                   dfs_mode_socket() const;
    int                       bytes_compressed() const;
    int                       bytes_outgoing() const;
    int                       bytes_incoming() const;
    bool                      is_constant() const;
    void                      set_constant(bool isConstant);
    SocketMode                mode() {
        return mode_;
    }
    SocketDirection direction() const {
        return direction_;
    }
    void set_direction(SocketDirection dir) {
        direction_ = dir;
    }

    std::uint64_t timestamp() const;

    // Snapshot of peer attributes derived from the initial handshake.
    // Populated by check_first_message(); read by send/receive handlers to
    // decide on wire format and feature availability.
    const PeerMeta &peer_meta() const {
        return peer_meta_;
    }

public:
    virtual void flush()                                                                  = 0;
    virtual void send_message(const QByteArray &data, Priority priority = Priority::High) = 0;

    bool is_closed();

    long queue_size() {
        QMutexLocker locker(&queue_mutex_);
        return high_queue_.size() + normal_queue_.size() + low_queue_.size();
    }

    virtual qint64 pending_bytes() const {
        return queued_bytes_.load(std::memory_order_relaxed);
    }

    // protected slots:
public slots:
    virtual void closeSocket();

signals:
    void send(const QByteArray &data);
    void disconnected();
    void error(Network::SocketServiceError code,
               const QString              &errorData,
               std::string                 ip,
               std::string                 identifier,
               SocketDirection             direction);
    void close(Network::SocketServiceError code = Network::SocketServiceError::PhysicalKill);
    void activated();
    void finished(); // if threads
    void shareConnections(const std::set<SocketService::SocketPair> &);

protected:
    bool       check_first_message(const HandshakeMessage &msg);
    QByteArray generate_first_message();
    QByteArray prepareSendMessage(const QByteArray &message);
    QByteArray prepareReceiveMessage(const QByteArray &message);

    ExtraChainNode  *node;
    QString          identifier_;
    QString          ip_;
    quint16          port_             = 0;
    bool             activated_        = false;
    bool             is_disconnected_  = false;
    int              bytes_incoming_   = 0;
    int              bytes_outgoing_   = 0;
    int              bytes_compressed_ = 0;
    std::atomic_bool is_constant_      = false;
    std::uint64_t    timestamp_        = 0;
    SocketMode       mode_             = SocketMode::Full;
    SocketDirection  direction_        = SocketDirection::Outgoing;
    DfsMode          dfs_mode_socket_  = DfsMode::Full;
    PeerMeta         peer_meta_;

    QMutex                 queue_mutex_;
    std::queue<QByteArray> high_queue_;
    std::queue<QByteArray> normal_queue_;
    std::queue<QByteArray> low_queue_;
    std::atomic<qint64>    queued_bytes_ = 0;

    static constexpr qint64 MAX_BUFFER_SIZE       = 10 * 1024 * 1024; // 10MB
    bool                    waiting_buffer_space_ = false;

    KeyPrivate priv_   = KeyPrivate();
    KeyPublic  pub_    = KeyPublic();
    bool       is_pub_ = false;
    bool       closed_ = false;
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
                       node_version))

BOOST_DESCRIBE_STRUCT(SocketService::SocketPair, (), (ip, identifier))

#endif // ISOCKETSERVICE_H
