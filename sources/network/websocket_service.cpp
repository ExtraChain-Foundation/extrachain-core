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
#include <csignal>
#include <QMutexLocker>
#include <QNetworkProxy>

// Глобальная инициализация для игнорирования SIGPIPE
static bool sigpipe_initialized = false;
static void init_sigpipe_handling() {
    if (!sigpipe_initialized) {
#ifdef Q_OS_LINUX
        // Игнорируем SIGPIPE глобально для процесса
        signal(SIGPIPE, SIG_IGN);
#endif
        sigpipe_initialized = true;
    }
}

WebSocketService::WebSocketService(QWebSocket *ws, ExtraChainNode *node, QObject *parent, const bool is_constant)
    : SocketService(node, parent) {

    // Инициализируем обработку SIGPIPE
    init_sigpipe_handling();

    is_constant_ = is_constant;
    m_connectionMutex = new QMutex();

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

                QMutexLocker lock(m_connectionMutex);
                if (m_ws == nullptr || closed_) {
                    return;
                }

                if (!isSocketConnected()) {
                    closeSocketInternal();
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

                // Безопасная отправка с проверкой состояния
                if (safeSendTextMessage(QString::fromStdString(encoded))) {
                    safeFlush();
                }
                emit closeSocketSig();
            });

    connect(m_ws, &QWebSocket::pong, this, [this](quint64) {
        QMutexLocker lock(m_connectionMutex);
        m_failedPongs = 0;
    });

    setupPingTimer();
}

WebSocketService::~WebSocketService() {
    closeSocket();
    eLog("[WS] I'm socket, i'm death: {}", ip_);

    if (m_connectionMutex) {
        delete m_connectionMutex;
        m_connectionMutex = nullptr;
    }
}

void WebSocketService::setupPingTimer() {
    if (!m_pingTimer) {
        m_pingTimer = new QTimer(this);
        connect(m_pingTimer, &QTimer::timeout, this, [this]() {
            QMutexLocker lock(m_connectionMutex);
            if (m_ws && isSocketConnected() && activated_) {
                // Безопасный ping
                try {
                    m_ws->ping();
                    m_failedPongs++;

                    if (m_failedPongs > 3) {
                        eLog("[WS] Connection lost (no pong) from {}", ip_);
                        emit error(Network::SocketServiceError::PongLost,
                                   "No pong response",
                                   ip_.toStdString(),
                                   identifier_.toStdString());
                    }
                } catch (...) {
                    eLog("[WS] Exception during ping to {}", ip_);
                    emit error(Network::SocketServiceError::PongLost,
                               "Ping exception",
                               ip_.toStdString(),
                               identifier_.toStdString());
                }
            }
        });
        // m_pingTimer->start(3000);
    }
}

QWebSocket *WebSocketService::socket() const {
    QMutexLocker lock(m_connectionMutex);
    return m_ws;
}

bool WebSocketService::is_active() const {
    QMutexLocker lock(m_connectionMutex);
    return activated_ && m_ws && m_ws->isValid();
}

bool WebSocketService::isSocketConnected() const {
    return m_ws &&
           m_ws->isValid() &&
           m_ws->state() == QAbstractSocket::ConnectedState;
}

bool WebSocketService::isSocketValid() const {
    return m_ws && m_ws->isValid();
}

void WebSocketService::open(const QString &ip, quint16 port) {
    QMutexLocker lock(m_connectionMutex);

    if (m_ws && m_ws->isValid() && m_ws->state() == QAbstractSocket::ConnectedState) {
        eCritical("[WS] Already opened");
        closeSocketInternal();
    } else {
        timestamp_ = Utils::current_date_ms();

        auto url = QUrl(QString("ws://%1:%2").arg(ip).arg(port));
        eLog("[WS] Open {}", url);
        connections();

        try {
            m_ws->open(url);
            ip_ = ip;
        } catch (...) {
            eLog("[WS] Exception during socket open");
            closeSocketInternal();
        }
    }
}

QString WebSocketService::protocol_string() const {
    return "WebSocket";
}

Network::Protocol WebSocketService::protocol() const {
    return Network::Protocol::WebSocket;
}

void WebSocketService::closeSocket() {
    QMutexLocker lock(m_connectionMutex);
    closeSocketInternal();
}

void WebSocketService::closeSocketInternal() {
    if (closed_) {
        return;
    }

    activated_            = false;
    waiting_buffer_space_ = false;
    closed_               = true;

           // Очищаем очереди
    {
        QMutexLocker locker(&queue_mutex_);
        std::queue<QByteArray> empty1, empty2, empty3, empty4;
        high_queue_.swap(empty1);
        normal_queue_.swap(empty2);
        low_queue_.swap(empty3);
        m_messageCache.swap(empty4);
    }

           // Безопасно закрываем сокет
    if (m_ws) {
        try {
            if (m_ws->state() == QAbstractSocket::ConnectedState) {
                eLog("[WS] Close socket");
                m_ws->close();
            }

            m_ws->deleteLater();
            m_ws = nullptr;
        } catch (...) {
            eLog("[WS] Exception during socket close");
            m_ws = nullptr;
        }
    }

    if (!is_disconnected_) {
        is_disconnected_ = true;
        emit disconnected();
    }

    if (m_pingTimer) {
        m_pingTimer->stop();
        m_pingTimer->deleteLater();
        m_pingTimer = nullptr;
    }
}

bool WebSocketService::operator==(const WebSocketService &service) const {
    QMutexLocker lock1(m_connectionMutex);
    QMutexLocker lock2(service.m_connectionMutex);
    return m_ws == service.m_ws;
}

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
        eLog("[WS] Message cached until activation. Cache size: {}", m_messageCache.size());
        return;
    }

    processMessage(message);
}

void WebSocketService::processMessage(const QByteArray &message) {
    auto mess = prepareReceiveMessage(message);

    if (!node_enabled) {
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
    }

    if (!waiting_buffer_space_) {
        emit needToTryDequeue();
    }
}

bool WebSocketService::canSendMore() const {
    QMutexLocker lock(m_connectionMutex);

    if (!isSocketConnected()) {
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

bool WebSocketService::safeSendTextMessage(const QString &message) {
    if (message.isEmpty()) {
        return false;
    }

           // Проверяем состояние сокета без мьютекса для избежания deadlock
    if (!m_ws) {
        return false;
    }

    try {
        qint64 written = m_ws->sendTextMessage(message);
        if (written < 0) {
            eCritical("[WS] Failed to send text message");
            emit error(Network::SocketServiceError::CantSend, "", ip_.toStdString(), identifier_.toStdString());
            return false;
        }
        return true;
    } catch (...) {
        eCritical("[WS] Exception during text message send");
        emit error(Network::SocketServiceError::CantSend, "", ip_.toStdString(), identifier_.toStdString());
        return false;
    }
}

bool WebSocketService::safeSendBinaryMessage(const QByteArray &message) {
    if (message.isEmpty()) {
        return false;
    }

           // Проверяем состояние сокета без мьютекса для избежания deadlock
    if (!m_ws) {
        return false;
    }

    try {
        qint64 written = m_ws->sendBinaryMessage(message);
        if (written < 0) {
            eCritical("[WS] Failed to send binary message");
            emit error(Network::SocketServiceError::CantSend, "", ip_.toStdString(), identifier_.toStdString());
            return false;
        }
        return true;
    } catch (...) {
        eCritical("[WS] Exception during binary message send");
        emit error(Network::SocketServiceError::CantSend, "", ip_.toStdString(), identifier_.toStdString());
        return false;
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

    if (!safeSendBinaryMessage(prepared)) {
        eLog("[WS] Failed to send prepared message");
    }
}

void WebSocketService::flush() {
    safeFlush();
}

void WebSocketService::safeFlush() {
    QMutexLocker lock(m_connectionMutex);

    if (!isSocketConnected() || !activated_ || m_ws->bytesToWrite() == 0) {
        return;
    }

    try {
        m_ws->flush();
    } catch (...) {
        eLog("[WS] Exception during flush");
    }
}

void WebSocketService::onConnected() {
    // НЕ блокируем мьютекс здесь, чтобы избежать deadlock при установке соединения
    if (!m_ws || !m_ws->isValid()) {
        return;
    }

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
    if (!m_ws) return;

    connect(m_ws, &QWebSocket::connected, this, &WebSocketService::onConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &WebSocketService::closeSocket);
    connect(this, &WebSocketService::closeSocketSig, this, &WebSocketService::closeSocket);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &WebSocketService::onTextMessage, Qt::QueuedConnection);
    connect(m_ws,
            &QWebSocket::binaryMessageReceived,
            this,
            &WebSocketService::onBinaryMessage,
            Qt::QueuedConnection);
    connect(this, &WebSocketService::close, [this](Network::SocketServiceError code) {
        emit error(code, "", ip_.toStdString(), identifier_.toStdString());
    });
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

    if (!safeSendTextMessage(QString::fromStdString(pub_key_str))) {
        eCritical("[WS] Handshake send failed");
        emit error(Network::SocketServiceError::IncorrectHandshake,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
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

    if (!safeSendTextMessage(QString::fromStdString(encoded_json))) {
        eCritical("[WS] Handshake send failed");
        emit error(Network::SocketServiceError::IncorrectHandshake,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
    }
}

quint16 WebSocketService::port() const {
    QMutexLocker lock(m_connectionMutex);

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
    QMutexLocker locker(&queue_mutex_);

    while (!m_messageCache.empty()) {
        eLog("-------------------------------- processCachedMessages");
        auto message = m_messageCache.front();
        m_messageCache.pop();

        // Временно разблокируем мьютекс для обработки сообщения
        locker.unlock();
        processMessage(message);
        locker.relock();
    }

    std::queue<QByteArray> empty;
    m_messageCache.swap(empty);
}
