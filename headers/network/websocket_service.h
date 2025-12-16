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

#include "managers/extrachain_node.h"
#include "network/isocket_service.h"
#include "network/network_manager.h"
#include "utils/exc_utils.h"
#include <QWebSocket>

#include "extrachain_global.h"

class EXTRACHAIN_EXPORT WebSocketService : public SocketService {
    Q_OBJECT

public:
    explicit WebSocketService(QWebSocket     *ws,
                              ExtraChainNode *node,
                              QObject        *parent      = nullptr,
                              const bool      is_constant = false,
                              const bool      is_light    = false);
    // WebSocketService(const WebSocketService &);
    ~WebSocketService();

    QWebSocket               *socket() const;
    bool                      is_active() const override;
    void                      open(const QString &ip, quint16 port);
    virtual QString           protocol_string() const override;
    virtual Network::Protocol protocol() const override;

    bool operator==(const WebSocketService &service) const;

    quint16 port() const override;
    quint16 server_port() const override;

public:
    void send_message(const QByteArray &data, Priority priority = Priority::High) override;

    virtual void flush() override;

signals:
    void sendMessageInternal(const QByteArray &data);
    void needToTryDequeue();
    void closeSocketSig();

private slots:
    void onTextMessage(const QString &message);
    void onBinaryMessage(const QByteArray &message);

    void onConnected();
    void onSocketError(QAbstractSocket::SocketError error);
public slots:
    void closeSocket() override;
private slots:
    void sendMessageInternalSlot(const QByteArray &data);
    void tryDequeueMessage();

private:
    void connections();
    void send_public_key();
    void handshake();
    bool canSendMore() const;
    void processMessage(const QByteArray &message);
    void processCachedMessages();

    QWebSocket *m_ws = nullptr;

    QTimer                *m_pingTimer   = nullptr;
    int                    m_failedPongs = 0;
    std::queue<QByteArray> m_messageCache;
};
