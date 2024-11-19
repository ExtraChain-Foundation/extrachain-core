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

    explicit SocketService(ExtraChainNode *node, QObject *parent = nullptr);
    const QString &identifier() const;
    virtual QString protocolString() const = 0;
    virtual Network::Protocol protocol() const = 0;
    virtual bool isActive() const = 0;
    virtual quint16 port() const = 0;
    virtual quint16 serverPort() const = 0;
    const QString &ip() const;
    const SendType sendType() const;
    int bytesCompressed() const;
    int bytesOutgoing() const;
    int bytesIncoming() const;
    bool                      isConstant() const;
    void                      setConstant(bool isConstant);

public:
    virtual void sendMessage(const QByteArray &data) = 0;
    virtual void final() = 0;

protected slots:
    virtual void closeSocket();

signals:
    void send(const QByteArray &data);
    void disconnected();
    void error(Network::SocketServiceError code, const QString &errorData);
    void close();
    void activated();
    void finished(); // if threads
    void shareConnections(const QJsonArray connectionsArr);

protected:
    bool       checkFirstMessage(const QString &message, const bool canUseConnection);
    QByteArray generateFirstMessage();
    QByteArray prepareSendMessage(const QByteArray &message);
    QByteArray prepareReceiveMessage(const QByteArray &message);

    ExtraChainNode *node;
    QString m_identifier;
    QString m_ip;
    quint16 m_port = 0;
    bool m_activated = false;
    bool            m_needToDelete;
    int m_bytesIncoming = 0;
    int m_bytesOutgoing = 0;
    int m_bytesCompressed = 0;
    SendType m_sendType = SendType::All;
    std::atomic_bool m_isConstant      = false;
    // ActorId subNetwork;

    KeyPrivate priv;
    KeyPublic pub;
};

#endif // WEBSOCKETSERVICE_H
