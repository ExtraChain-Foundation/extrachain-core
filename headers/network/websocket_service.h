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
                              QObject        *parent       = nullptr,
                              const bool      isConstant   = false,
                              const bool      needToDelete = false);
    // WebSocketService(const WebSocketService &);
    ~WebSocketService();

    QWebSocket     *socket() const;
    bool            isActive() const override;
    void            open(const QString &ip, quint16 port);
    virtual QString protocolString() const override {
        return "WebSocket";
    }
    virtual Network::Protocol protocol() const override {
        return Network::Protocol::WebSocket;
    }

    bool operator==(const WebSocketService &service) const;

    quint16 port() const override;
    quint16 serverPort() const override;

public:
    virtual void sendMessage(const QByteArray &data) override;
    void         sendMessageQuality(const QByteArray &data, Priority priority = Priority::High) override;
    virtual void final() override;

signals:
    void sendMessageInternal(const QByteArray &data);

private slots:
    void onTextMessage(const QString &message);
    void onBinaryMessage(const QByteArray &message);

    void onConnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void closeSocket() override;
    void sendMessageInternalSlot(const QByteArray &data);

private:
    void connections();
    void handshake();

    QWebSocket *m_ws = nullptr;

    QTimer m_timer;

    void tryDequeueMessage();
    bool canSendMore() const;

    QQueue<QByteArray> m_messageCache;
    void               processMessage(const QByteArray &message);
    void               processCachedMessages();
};
