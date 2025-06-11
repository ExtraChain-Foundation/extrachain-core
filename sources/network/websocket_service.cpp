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

WebSocketService::WebSocketService(QWebSocket *ws, ExtraChainNode *node, QObject *parent, const bool is_constant)
    : SocketService(node, parent) {
    is_constant_ = is_constant;

    if (ws == nullptr) {
        m_ws = new QWebSocket("ExtraChain");
        eLog("[WS] Create new ws");
    } else {
        // from server
        timestamp_  = Utils::current_date_ms();
        m_ws        = ws;
        this->ip_   = m_ws->peerAddress().toString().replace("::ffff:", "");
        this->port_ = m_ws->peerPort();
        eLog("[WS] New service: {}", ip_);
        connections();
        send_public_key();
    }

    connect(this,
            &WebSocketService::sendMessageInternal,
            this,
            &WebSocketService::sendMessageInternalSlot,
            Qt::QueuedConnection);
    connect(this,
            &WebSocketService::needToTryDequeue,
            this,
            &WebSocketService::tryDequeueMessage,
            Qt::QueuedConnection);

    connect(this,
            &WebSocketService::error,
            [this](Network::SocketServiceError code,
                   const QString              &errorData,
                   std::string                 ip,
                   std::string                 identifier) {
                if (m_ws == nullptr) {
                    return;
                }

                if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
                    closeSocket();
                    return;
                }

                if (m_ws->error() != QAbstractSocket::UnknownSocketError) {
                    closeSocket();
                    return;
                }

                auto code_string = QByteArray::number(std::to_underlying(code));
                auto encrypted   = prepareSendMessage("Error " + code_string);
                if (encrypted.isEmpty()) {
                    eLog("[WS] Error not sended (to ip: {}, id: {}): {}", ip_, identifier_, code);
                    emit closeSocketSig();
                    return;
                }
                auto encoded = Utils::to_base64(encrypted.toStdString());
                auto written = m_ws->sendTextMessage(QString::fromStdString(encoded));
                // m_ws->flush();
                flush();
                // eLog("[WS] Error sended (to ip: {}, id: {}): {}", ip_, identifier_, code);
                emit closeSocketSig();
            });

    connect(m_ws, &QWebSocket::pong, this, [this](quint64) {
        // eLog("[WS] Pong {}", ip());
        m_failedPongs = 0;
    });

    if (!m_pingTimer) {
        m_pingTimer = new QTimer(this);
        connect(m_pingTimer, &QTimer::timeout, this, [this]() {
            if (m_ws && m_ws->isValid() && activated_) {
                m_ws->ping();
                // eLog("[WS] Ping {}", ip());
                m_failedPongs++;

                if (m_failedPongs > 3) {
                    eLog("[WS] Connection lost (no pong) from {}", ip_);
                    emit error(Network::SocketServiceError::PongLost,
                               "No pong response",
                               ip_.toStdString(),
                               identifier_.toStdString());
                }
            }
        });
        // m_pingTimer->start(3000);
    }
}

WebSocketService::~WebSocketService() {
    closeSocket();
    eLog("[WS] I'm socket, i'm death: {}", ip_);
}

QWebSocket *WebSocketService::socket() const {
    return m_ws;
}

bool WebSocketService::is_active() const {
    return activated_ && m_ws->isValid();
}

void WebSocketService::open(const QString &ip, quint16 port) {
    if (m_ws->isValid()) {
        eCritical("[WS] Already opened");
        closeSocket();
    } else {
        timestamp_ = Utils::current_date_ms();

        auto url = QUrl(QString("ws://%1:%2").arg(ip).arg(port));
        eLog("[WS] Open {}", url);
        connections();
        m_ws->open(url);
        ip_ = ip; // m_ws->peerAddress().toString();

        // port_ = m_ws->peerPort();

        // QTimer *timeout = new QTimer(this);
        // timeout->setSingleShot(true);

        // connect(timeout, &QTimer::timeout, this, [this, timeout]() {
        //     eLog("[WS] Connection timeout");
        //     timeout->deleteLater();
        //     closeSocket();
        // });

        // connect(m_ws, &QWebSocket::connected, timeout, [timeout]() {
        //     timeout->stop();
        //     timeout->deleteLater();
        // });
    }
}

QString WebSocketService::protocol_string() const {
    return "WebSocket";
}

Network::Protocol WebSocketService::protocol() const {
    return Network::Protocol::WebSocket;
}

void WebSocketService::closeSocket() {
    activated_            = false;
    waiting_buffer_space_ = false;
    closed_               = true;

    {
        QMutexLocker           locker(&queue_mutex_);
        std::queue<QByteArray> empty1, empty2, empty3, empty4;
        high_queue_.swap(empty1);
        normal_queue_.swap(empty2);
        low_queue_.swap(empty3);
        m_messageCache.swap(empty4);
        locker.unlock();
    }

    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState) {
        eLog("[WS] Close socket");
        m_ws->close();
    }

    if (m_ws != nullptr) {
        // eLog("[WS] Delete socket pointer");
        m_ws->deleteLater();
        m_ws = nullptr;
    }

    if (!is_disconnected_) {
        // eLog("[WS] Disconnect socket");
        is_disconnected_ = true;
        emit disconnected();
        // m_ws->disconnect();
    }

    if (m_pingTimer != nullptr) {
        m_pingTimer->stop();
        m_pingTimer->deleteLater();
        m_pingTimer = nullptr;
    }
}

bool WebSocketService::operator==(const WebSocketService &service) const {
    return m_ws == service.m_ws;
}

// for first message
void WebSocketService::onTextMessage(const QString &message) {
    m_failedPongs = 0;

    if (message.isEmpty())
        return;

    if (!is_pub_) {
        auto pub_result = Utils::from_base64(message.toStdString());
        if (!pub_result.has_value()) {
            emit error(Network::SocketServiceError::IncorrectPublicKey,
                       "",
                       ip_.toStdString(),
                       identifier_.toStdString());
            return;
        }

        pub_    = KeyPublic(ByteArray(pub_result.value()).toArray<crypto_sign_PUBLICKEYBYTES>());
        is_pub_ = true;

        handshake();
        return;
    }

    auto encoded_result = Utils::from_base64(message.toStdString());
    if (!encoded_result.has_value()) {
        emit error(Network::SocketServiceError::IncorrectFirstMessage,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
        return;
    }

    auto decoded = prepareReceiveMessage(QByteArray::fromStdString(encoded_result.value()));
    if (decoded.isEmpty()) {
        eLog("[WS] Failed to decode message");
        emit error(Network::SocketServiceError::IncorrectPublicKey,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
        return;
    }

    if (decoded.contains("Error ")) {
        auto error = Network::SocketServiceError(decoded.mid(6).toInt());
        eLog("[WS] Error received (from ip: {}, id: {}): {}", ip_, identifier_, error);
        closeSocket();
        return;
    }

    auto handshake_result = Json::deserialize<HandshakeMessage>(decoded.toStdString());
    if (!handshake_result.has_value()) {
        eLog("[WS] Failed to parse handshake message: {}", handshake_result.error());
        emit error(Network::SocketServiceError::IncorrectFirstMessage,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
        return;
    }

    bool checked = check_first_message(handshake_result.value());
    if (checked) {
        processCachedMessages();
    }
}

void WebSocketService::onBinaryMessage(const QByteArray &message) {
    m_failedPongs = 0;

    if (!activated_) {
        QMutexLocker locker(&queue_mutex_);
        m_messageCache.push(message);
        locker.unlock();

        eLog("[WS] Message cached until activation. Cache size: {}", m_messageCache.size());
        return;
    }
    // eFatal("[WS] Binary: not activated");

    processMessage(message);
}

void WebSocketService::processMessage(const QByteArray &message) {
    auto mess = prepareReceiveMessage(message);

    if (!node_enabled) {
        // emit error(Network::SocketServiceError::PhysicalKill, "", ip_.toStdString(), identifier_.toStdString());
        return;
    }

    if (!mess.isEmpty()) {
        node->network()->messageReceived(mess.toStdString(), ip_.toStdString(), identifier_.toStdString());
    } else {
        eCritical("[WS] Message is empty after prepare");
        emit error(Network::SocketServiceError::EmptyMessage, "", ip_.toStdString(), identifier_.toStdString());
    }
}

void WebSocketService::send_message(const QByteArray &data, Priority priority) {
    if (!is_active() || closed_) {
        eLog("[WS] Try to send without activation {}", data.left(35));
        return;
    }

    if (data.isEmpty()) {
        eCritical("[WS] Error send size");
        emit error(Network::SocketServiceError::IncorrectMessage,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
        return;
    }

    {
        QMutexLocker locker(&queue_mutex_);
        switch (priority) {
        case Priority::High:
            high_queue_.push(data);
            break;
        case Priority::Normal:
            normal_queue_.push(data);
            break;
        case Priority::Low:
            low_queue_.push(data);
            break;
        }
        locker.unlock();
    }

    if (!waiting_buffer_space_) {
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
    if (closed_) {
        return;
    }

    if (!canSendMore()) {
        waiting_buffer_space_ = true;
        return;
    }

    waiting_buffer_space_ = false;
    QMutexLocker locker(&queue_mutex_);

    QByteArray data;
    if (!high_queue_.empty()) {
        data = high_queue_.front();
        high_queue_.pop();
    } else if (!normal_queue_.empty()) {
        data = normal_queue_.front();
        normal_queue_.pop();
    } else if (!low_queue_.empty()) {
        data = low_queue_.front();
        low_queue_.pop();
    }

    locker.unlock();

    if (!data.isEmpty()) {
        emit sendMessageInternal(data);

        if (!high_queue_.empty() || !normal_queue_.empty() || !low_queue_.empty()) {
            emit needToTryDequeue();
        }
    }
}

void WebSocketService::sendMessageInternalSlot(const QByteArray &data) {
    if (data.isEmpty() || closed_) {
        return;
    }

    auto prepared = prepareSendMessage(data);
    if (prepared.isEmpty()) {
        return;
    }

    if (m_ws == nullptr) {
        return;
    }
    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState || !activated_) {
        return;
    }

    if (m_ws->error() != QAbstractSocket::UnknownSocketError) {
        eLog("[WS] Socket has error: {}", m_ws->error());
        closeSocket();
        return;
    }

    qint64 written = m_ws->sendBinaryMessage(prepared);
    if (written < 0) {
        eCritical("[WS] Failed to send message");
        emit error(Network::SocketServiceError::CantSend, "", ip_.toStdString(), identifier_.toStdString());
    }
}

void WebSocketService::flush() {
    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    if (!this->activated_ || m_ws->bytesToWrite() == 0) {
        return;

    }

    if (m_ws->error() != QAbstractSocket::UnknownSocketError) {
        eLog("[WS] Socket has error, skipping flush");
        return;
    }

    m_ws->flush();
}

void WebSocketService::onConnected() {
    // from local connect
    this->ip_   = m_ws->peerAddress().toString().replace("::ffff:", "");
    this->port_ = m_ws->peerPort();
    send_public_key();
    eLog("[WS] New service: {} {}", ip_, port());
}

void WebSocketService::onSocketError(QAbstractSocket::SocketError error) {
    eLog("[WS] Socket error: {}, {}", Utils::enum_value_name(error), ip_);
    closeSocket();
}

void WebSocketService::connections() {
    connect(m_ws, &QWebSocket::connected, this, &WebSocketService::onConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &WebSocketService::closeSocket);
    connect(this, &WebSocketService::closeSocketSig, this, &WebSocketService::closeSocket);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &WebSocketService::onTextMessage, Qt::QueuedConnection);
    connect(m_ws,
            &QWebSocket::binaryMessageReceived,
            this,
            &WebSocketService::onBinaryMessage,
            Qt::QueuedConnection);
    // connect(this, &WebSocketService::send, this, &WebSocketService::sendMessage);
    connect(this, &WebSocketService::close, [this](Network::SocketServiceError code) {
        emit error(code, "", ip_.toStdString(), identifier_.toStdString());
    }); // slot
    connect(m_ws, &QWebSocket::errorOccurred, this, &WebSocketService::onSocketError);
    connect(m_ws, &QWebSocket::bytesWritten, this, [this](qint64) {
        if (waiting_buffer_space_) {
            emit needToTryDequeue();
        }
    });
}

void WebSocketService::send_public_key() {
    auto pub_key_str = Utils::to_base64(ByteArray(priv_.public_key()).toString());

    if (m_ws == nullptr) {
        return;
    }
    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
        closeSocket();
        return;
    }

    if (m_ws->error() != QAbstractSocket::UnknownSocketError) {
        closeSocket();
        return;
    }

    auto written = m_ws->sendTextMessage(QString::fromStdString(pub_key_str));
    if (written < 0) {
        eCritical("[WS] Handshake send failed");
        emit error(Network::SocketServiceError::IncorrectHandshake,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
        return;
    }
}

void WebSocketService::handshake() {
    auto first_message = generate_first_message();
    auto encrypted     = prepareSendMessage(first_message);
    if (encrypted.isEmpty()) {
        emit error(Network::SocketServiceError::IncorrectHandshake,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
        return;
    }
    auto encoded_json = Utils::to_base64(encrypted.toStdString());

    if (m_ws == nullptr) {
        return;
    }

    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
        closeSocket();
        return;
    }

    if (m_ws->error() != QAbstractSocket::UnknownSocketError) {
        closeSocket();
        return;
    }

    auto written = m_ws->sendTextMessage(QString::fromStdString(encoded_json));
    if (written < 0) {
        eCritical("[WS] Handshake send failed");
        emit error(Network::SocketServiceError::IncorrectHandshake,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
        return;
    }
}

quint16 WebSocketService::port() const {
    if (m_ws == nullptr) {
        return 0;
    }
    if (m_ws->peerPort() != node->network()->wsPort)
        return m_ws->peerPort();
    else
        return m_ws->localPort();
}

quint16 WebSocketService::server_port() const {
    return node->network()->wsPort;
}

void WebSocketService::processCachedMessages() {
    while (!m_messageCache.empty()) {
        eLog("-------------------------------- processCachedMessages");
        auto message = m_messageCache.front();
        m_messageCache.pop();
        processMessage(message);
    }

    std::queue<QByteArray> empty;
    m_messageCache.swap(empty);
}
