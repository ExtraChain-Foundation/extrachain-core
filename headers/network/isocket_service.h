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

#include "boost/describe.hpp"
#include "encryption/key_private.h"
#include "encryption/key_public.h"
#include "utils/exc_utils.h"

#include <QQueue>
#include <QMutex>

class ExtraChainNode;

class EXTRACHAIN_EXPORT SocketService : public QObject {
    Q_OBJECT

public:
    enum class SocketType {
        Full,
        Part,
        None,
    };
    Q_ENUM(SocketType)

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
        std::string          version;
        std::string          identifier;
        SocketType           socket_type = SocketType::Full;
        std::string          your_ip;
        std::set<SocketPair> connections;
        bool                 is_available = false;
        bool                 is_constant  = false;
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
    const SocketType          socket_type() const;
    int                       bytes_compressed() const;
    int                       bytes_outgoing() const;
    int                       bytes_incoming() const;
    bool                      is_constant() const;
    void                      set_constant(bool isConstant);
    bool                      is_vpn() const;
    void                      set_vpn(bool isVPN);

public:
    virtual void flush()                                                                  = 0;
    virtual void send_message(const QByteArray &data, Priority priority = Priority::High) = 0;

protected slots:
    virtual void closeSocket();

signals:
    void send(const QByteArray &data);
    void disconnected();
    void error(Network::SocketServiceError code, const QString &errorData, std::string ip, std::string identifier);
    void close();
    void activated();
    void finished(); // if threads
    void shareConnections(const std::set<SocketPair> &);

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
    SocketType       socket_type_      = SocketType::Full; // TODO: this is for socket, need also global
    std::atomic_bool is_constant_      = false;
    std::atomic_bool is_vpn_           = false;

    QMutex             queue_mutex_;
    QQueue<QByteArray> high_queue_;
    QQueue<QByteArray> normal_queue_;
    QQueue<QByteArray> low_queue_;

    static constexpr qint64 MAX_BUFFER_SIZE       = 10 * 1024 * 1024; // 10MB
    bool                    waiting_buffer_space_ = false;

    KeyPrivate priv_   = KeyPrivate();
    KeyPublic  pub_    = KeyPublic();
    bool       is_pub_ = false;
};

BOOST_DESCRIBE_STRUCT(SocketService::HandshakeMessage,
                      (),
                      (network_id, version, identifier, socket_type, your_ip, connections, is_available))

BOOST_DESCRIBE_STRUCT(SocketService::SocketPair, (), (ip, identifier))

#endif // ISOCKETSERVICE_H
