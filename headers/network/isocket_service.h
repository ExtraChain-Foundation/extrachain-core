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

#include <QQueue>

class ExtraChainNode;

class EXTRACHAIN_EXPORT SocketService : public QObject {
    Q_OBJECT

public:
    enum class SendType {
        All,
        None,
        // OnlySubNetwork
    };
    Q_ENUM(SendType)

    struct HandshakeMessage {
        std::string              first_id;
        std::string              version;
        std::string              identifier;
        SendType                 send_type = SendType::All;
        std::vector<std::string> connections;
        bool                     is_available = false;
        bool                     is_constant  = false;
    };

    enum class Priority {
        Low,
        Normal,
        High
    };

    explicit SocketService(ExtraChainNode *node, QObject *parent = nullptr);
    const QString            &identifier() const;
    virtual QString           protocolString() const = 0;
    virtual Network::Protocol protocol() const       = 0;
    virtual bool              isActive() const       = 0;
    virtual quint16           port() const           = 0;
    virtual quint16           serverPort() const     = 0;
    const QString            &ip() const;
    const SendType            sendType() const;
    int                       bytesCompressed() const;
    int                       bytesOutgoing() const;
    int                       bytesIncoming() const;
    bool                      isConstant() const;
    void                      setConstant(bool isConstant);
    bool                      isVPN() const;
    void                      setVPN(bool isVPN);

public:
    virtual void final()                                                                 = 0;
    virtual void sendMessage(const QByteArray &data, Priority priority = Priority::High) = 0;

protected slots:
    virtual void closeSocket();

signals:
    void send(const QByteArray &data);
    void disconnected();
    void error(Network::SocketServiceError code, const QString &errorData);
    void close();
    void activated();
    void finished(); // if threads
    void shareConnections(const std::vector<std::string> &);

protected:
    bool       checkFirstMessage(const HandshakeMessage &msg);
    QByteArray generateFirstMessage();
    QByteArray prepareSendMessage(const QByteArray &message);
    QByteArray prepareReceiveMessage(const QByteArray &message);

    ExtraChainNode  *node;
    QString          m_identifier;
    QString          m_ip;
    quint16          m_port          = 0;
    bool             m_activated     = false;
    bool             is_disconnected = false;
    bool             m_needToDelete;
    int              m_bytesIncoming   = 0;
    int              m_bytesOutgoing   = 0;
    int              m_bytesCompressed = 0;
    SendType         m_sendType        = SendType::All;
    std::atomic_bool m_isConstant      = false;
    std::atomic_bool m_isVPN           = false;
    // ActorId subNetwork;

    QMutex             m_queueMutex;
    QQueue<QByteArray> m_highQueue;
    QQueue<QByteArray> m_normalQueue;
    QQueue<QByteArray> m_lowQueue;

    static constexpr qint64 MAX_BUFFER_SIZE         = 10 * 1024 * 1024; // 10MB
    bool                    m_waitingForBufferSpace = false;

    KeyPrivate priv   = KeyPrivate();
    KeyPublic  pub    = KeyPublic();
    bool       is_pub = false;
};

BOOST_DESCRIBE_STRUCT(SocketService::HandshakeMessage,
                      (),
                      (first_id, version, identifier, send_type, connections, is_available))

#endif // ISOCKETSERVICE_H
