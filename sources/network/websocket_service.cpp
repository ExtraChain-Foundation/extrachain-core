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

#include "network/websocket_service.h"

WebSocketService::WebSocketService(QWebSocket     *ws,
                                   ExtraChainNode *node,
                                   QObject        *parent,
                                   const bool      isConstant,
                                   const bool      needToDelete)
    : SocketService(node, parent) {
    m_isConstant   = isConstant;
    m_needToDelete = needToDelete;
    if (ws == nullptr) {
        m_ws = new QWebSocket("ExtraChain");
        eLog("[WS] Create new ws");
    } else {
        // from server
        m_ws         = ws;
        this->m_ip   = m_ws->peerAddress().toString().replace("::ffff:", "");
        this->m_port = m_ws->peerPort();
        eLog("[WS] New service: {}", m_ip);
        connections();
        send_public_key();
    }

    connect(this,
            &WebSocketService::sendMessageInternal,
            this,
            &WebSocketService::sendMessageInternalSlot,
            Qt::DirectConnection);
    connect(this,
            &WebSocketService::needToTryDequeue,
            this,
            &WebSocketService::tryDequeueMessage,
            Qt::QueuedConnection);
}

WebSocketService::~WebSocketService() {
    eLog("[WS] I'm socket, i'm death");
}

QWebSocket *WebSocketService::socket() const {
    return m_ws;
}

bool WebSocketService::isActive() const {
    return m_activated && m_ws->isValid();
}

void WebSocketService::open(const QString &ip, quint16 port) {
    if (m_ws->isValid()) {
        eFatal("[WS] Already opened");
    } else {
        auto url = QUrl(QString("ws://%1:%2").arg(ip).arg(port));
        eLog("[WS] Open {}", url);
        connections();
        m_ws->open(url);
        m_ip   = m_ws->peerAddress().toString();
        m_port = m_ws->peerPort();
    }
}

void WebSocketService::closeSocket() {
    m_activated             = false;
    m_waitingForBufferSpace = false;

    {
        QMutexLocker locker(&m_queueMutex);
        m_highQueue.clear();
        m_normalQueue.clear();
        m_lowQueue.clear();
        m_messageCache.clear();
    }

    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState) {
        eLog("[WS] Close socket");
        m_ws->close();
    }

    if (m_ws != nullptr) {
        eLog("[WS] Delete socket");
        m_ws->deleteLater();
        m_ws = nullptr;
    }

    if (!is_disconnected) {
        eLog("[WS] Disconnect socket");
        is_disconnected = true;
        emit disconnected();
        m_ws->disconnect();
    }
}

bool WebSocketService::operator==(const WebSocketService &service) const {
    return m_ws == service.m_ws;
}

// for first message
void WebSocketService::onTextMessage(const QString &message) {
    if (message.isEmpty())
        return;

    if (!is_pub) {
        pub    = KeyPublic(ByteArray::fromBase64(message).toArray<crypto_sign_PUBLICKEYBYTES>());
        is_pub = true;

        handshake();
        return;
    }

    auto decoded = prepareReceiveMessage(ByteArray::fromBase64(message).toQByteArray());
    if (decoded.isEmpty()) {
        eLog("[WS] Failed to decode message");
        emit error(Network::SocketServiceError::IncorrectPublicKey, "");
        closeSocket();
        return;
    }

    auto handshake_result = Json::deserialize<HandshakeMessage>(decoded.toStdString());
    if (!handshake_result.has_value()) {
        eLog("[WS] Failed to parse handshake message: {}", handshake_result.error());
        emit error(Network::SocketServiceError::IncorrectFirstMessage, "");
        closeSocket();
        return;
    }

    bool checked = checkFirstMessage(handshake_result.value());
    if (checked) {
        processCachedMessages();
    }
}

void WebSocketService::onBinaryMessage(const QByteArray &message) {
    if (!m_activated) {
        QMutexLocker locker(&m_queueMutex);
        m_messageCache.enqueue(message);
        eLog("[WS] Message cached until activation. Cache size: {}", m_messageCache.size());
        return;
    }
    // eFatal("[WS] Binary: not activated");

    processMessage(message);
}

void WebSocketService::processMessage(const QByteArray &message) {
    auto mess = prepareReceiveMessage(message);
    if (!mess.isEmpty()) {
        node->network()->messageReceived(mess.toStdString(), m_ip.toStdString(), m_identifier.toStdString());
    } else {
        eFatal("[WS] Message is empty after prepare");
    }
}

void WebSocketService::sendMessage(const QByteArray &data, Priority priority) {
    if (!isActive()) {
        eLog("[WS] Try to send without activation {}", data.left(35));
        return;
    }
    if (data.isEmpty()) {
        eFatal("[WS] Error send size");
        return;
    }

    {
        QMutexLocker locker(&m_queueMutex);
        switch (priority) {
        case Priority::High:
            m_highQueue.enqueue(data);
            break;
        case Priority::Normal:
            m_normalQueue.enqueue(data);
            break;
        case Priority::Low:
            m_lowQueue.enqueue(data);
            break;
        }
    }

    if (!m_waitingForBufferSpace) {
        emit needToTryDequeue();
    }
}

bool WebSocketService::canSendMore() const {
    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
        return false;
    }

    if (m_ws->error() != QAbstractSocket::UnknownSocketError) {
        return false;
    }

    return m_ws->bytesToWrite() < MAX_BUFFER_SIZE;
}

void WebSocketService::tryDequeueMessage() {
    if (!canSendMore()) {
        m_waitingForBufferSpace = true;
        emit needToTryDequeue();
        return;
    }

    m_waitingForBufferSpace = false;
    QMutexLocker locker(&m_queueMutex);

    QByteArray data;
    if (!m_highQueue.isEmpty()) {
        data = m_highQueue.dequeue();
    } else if (!m_normalQueue.isEmpty()) {
        data = m_normalQueue.dequeue();
    } else if (!m_lowQueue.isEmpty()) {
        data = m_lowQueue.dequeue();
    }

    if (!data.isEmpty()) {
        emit sendMessageInternal(data);

        if (!m_highQueue.isEmpty() || !m_normalQueue.isEmpty() || !m_lowQueue.isEmpty()) {
            emit needToTryDequeue();
        }
    }
}

void WebSocketService::sendMessageInternalSlot(const QByteArray &data) {
    if (data.isEmpty()) {
        return;
    }

    auto prepared = prepareSendMessage(data);
    if (prepared.isEmpty()) {
        return;
    }

    if (m_ws == nullptr) {
        return;
    }
    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState || !m_activated) {
        return;
    }

    qint64 written = m_ws->sendBinaryMessage(prepared);
    if (written < 0) {
        eCritical("[WS] Failed to send message");
        closeSocket();
    }
}

void WebSocketService::final() {
    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
        return;
    }
    if (!this->m_activated || m_ws->bytesToWrite() == 0) {
        return;
    }

    m_ws->flush();
}

void WebSocketService::onConnected() {
    // from local connect
    this->m_ip   = m_ws->peerAddress().toString().replace("::ffff:", "");
    this->m_port = m_ws->peerPort();
    send_public_key();
    eLog("[WS] New service: {} {}", m_ip, port());
}

void WebSocketService::onSocketError(QAbstractSocket::SocketError error) {
    eLog("[WS] Socket error: {}", Utils::enum_value_name(error));

    if (m_ws->state() != QAbstractSocket::ConnectedState) {
        closeSocket();
    }
}

void WebSocketService::connections() {
    connect(m_ws, &QWebSocket::connected, this, &WebSocketService::onConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &WebSocketService::closeSocket);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &WebSocketService::onTextMessage, Qt::QueuedConnection);
    connect(m_ws,
            &QWebSocket::binaryMessageReceived,
            this,
            &WebSocketService::onBinaryMessage,
            Qt::QueuedConnection);
    // connect(this, &WebSocketService::send, this, &WebSocketService::sendMessage);
    connect(this, &WebSocketService::close, this, &WebSocketService::closeSocket); // slot
    connect(m_ws, &QWebSocket::errorOccurred, this, &WebSocketService::onSocketError);
    connect(m_ws, &QWebSocket::bytesWritten, this, [this](qint64) {
        if (m_waitingForBufferSpace) {
            emit needToTryDequeue();
        }
    });
}

void WebSocketService::send_public_key() {
    auto pub_key_str = ByteArray(priv.public_key()).toBase64QString();

    if (m_ws == nullptr) {
        return;
    }
    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
        closeSocket();
        return;
    }

    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
        closeSocket();
        return;
    }

    auto written = m_ws->sendTextMessage(pub_key_str);
    if (written < 0) {
        eCritical("[WS] Handshake send failed");
        closeSocket();
        return;
    }
}

void WebSocketService::handshake() {
    auto first_message = generateFirstMessage();
    auto encrypted     = prepareSendMessage(first_message);
    if (encrypted.isEmpty()) {
        closeSocket();
        return;
    }
    auto encoded_json = ByteArray(encrypted).toBase64QString();

    if (m_ws == nullptr) {
        return;
    }
    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
        closeSocket();
        return;
    }

    auto written = m_ws->sendTextMessage(encoded_json);
    if (written < 0) {
        eCritical("[WS] Handshake send failed");
        closeSocket();
        return;
    }
}

quint16 WebSocketService::port() const {
    if (m_ws->peerPort() != node->network()->wsPort)
        return m_ws->peerPort();
    else
        return m_ws->localPort();
}

quint16 WebSocketService::serverPort() const {
    return node->network()->wsPort;
}

void WebSocketService::processCachedMessages() {
    while (!m_messageCache.isEmpty()) {
        eLog("-------------------------------- processCachedMessages");
        auto message = m_messageCache.dequeue();
        processMessage(message);
    }
    m_messageCache.clear();
}
