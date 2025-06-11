#include "network/websocket_service.h"
#include <csignal>

WebSocketService::WebSocketService(QWebSocket *ws, ExtraChainNode *node, QObject *parent, const bool is_constant)
    : SocketService(node, parent) {

    // Ignore SIGPIPE globally to prevent crashes on broken pipes
    signal(SIGPIPE, SIG_IGN);

    is_constant_ = is_constant;

    if (ws == nullptr) {
        m_ws = new QWebSocket("ExtraChain", QWebSocketProtocol::VersionLatest, this);
        eLog("[WS] Create new ws");
    } else {
        // from server
        timestamp_ = Utils::current_date_ms();
        m_ws = ws;
        m_ws->setParent(this);
        this->ip_ = m_ws->peerAddress().toString().replace("::ffff:", "");
        this->port_ = m_ws->peerPort();
        m_socketValid.store(m_ws->isValid());
        eLog("[WS] New service: {}", ip_);
        connections();
        send_public_key();
    }

           // Setup internal signals with queued connections for thread safety
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

           // Error handler with improved safety
    connect(this,
            &WebSocketService::error,
            [this](Network::SocketServiceError code,
                   const QString              &errorData,
                   std::string                 ip,
                   std::string                 identifier) {

                if (!isSocketValid()) {
                    closeSocket();
                    return;
                }

                auto code_string = QByteArray::number(std::to_underlying(code));
                auto encrypted = prepareSendMessage("Error " + code_string);
                if (encrypted.isEmpty()) {
                    eLog("[WS] Error not sent (to ip: {}, id: {}): {}", ip_, identifier_, code);
                    emit closeSocketSig();
                    return;
                }

                auto encoded = Utils::to_base64(encrypted.toStdString());
                bool sent = safeSocketSend([this, encoded]() {
                    return m_ws->sendTextMessage(QString::fromStdString(encoded));
                }, "error message");

                if (!sent) {
                    eLog("[WS] Failed to send error message");
                }

                emit closeSocketSig();
            });

           // Pong handler
    connect(m_ws, &QWebSocket::pong, this, [this](quint64) {
        m_failedPongs.store(0);
    });

           // Setup ping timer
    if (!m_pingTimer) {
        m_pingTimer = new QTimer(this);
        connect(m_pingTimer, &QTimer::timeout, this, [this]() {
            if (isSocketValid() && activated_) {
                bool sent = safeSocketSend([this]() {
                    m_ws->ping();
                    return 1; // ping doesn't return bytes written
                }, "ping");

                if (sent) {
                    int failedPongs = m_failedPongs.fetch_add(1) + 1;
                    if (failedPongs > MAX_FAILED_PONGS) {
                        eLog("[WS] Connection lost (no pong) from {}", ip_);
                        emit error(Network::SocketServiceError::PongLost,
                                   "No pong response",
                                   ip_.toStdString(),
                                   identifier_.toStdString());
                    }
                }
            }
        });
        m_pingTimer->start(PING_INTERVAL_MS);
    }
}

WebSocketService::~WebSocketService() {
    closeSocket();
    eLog("[WS] Socket destroyed: {}", ip_);
}

QWebSocket *WebSocketService::socket() const {
    QMutexLocker locker(&m_stateMutex);
    return m_ws;
}

bool WebSocketService::is_active() const {
    QMutexLocker locker(&m_stateMutex);
    return activated_ && m_socketValid.load();
}

bool WebSocketService::isSocketValid() const {
    QMutexLocker locker(&m_stateMutex);
    return m_ws &&
           m_ws->isValid() &&
           m_ws->state() == QAbstractSocket::ConnectedState &&
           m_ws->error() == QAbstractSocket::UnknownSocketError;
}

bool WebSocketService::safeSocketSend(const std::function<qint64()> &sendFunc, const QString &operation) {
    QMutexLocker sendLocker(&m_sendMutex);

    if (!isSocketValid()) {
        eLog("[WS] Cannot send {}: socket invalid", operation);
        return false;
    }

    try {
        qint64 result = sendFunc();
        if (result < 0) {
            eLog("[WS] Failed to send {}: {}", operation, m_ws->errorString());
            QTimer::singleShot(0, this, &WebSocketService::closeSocket);
            return false;
        }
        return true;
    } catch (const std::exception &e) {
        eLog("[WS] Exception during {}: {}", operation, e.what());
        QTimer::singleShot(0, this, &WebSocketService::closeSocket);
        return false;
    } catch (...) {
        eLog("[WS] Unknown exception during {}", operation);
        QTimer::singleShot(0, this, &WebSocketService::closeSocket);
        return false;
    }
}

void WebSocketService::open(const QString &ip, quint16 port) {
    QMutexLocker locker(&m_stateMutex);

    if (m_ws && m_ws->isValid()) {
        eCritical("[WS] Already opened");
        closeSocket();
    }

    timestamp_ = Utils::current_date_ms();
    auto url = QUrl(QString("ws://%1:%2").arg(ip).arg(port));
    eLog("[WS] Opening {}", url.toString());

    connections();
    m_ws->open(url);
    ip_ = ip;
    m_socketValid.store(false); // Will be set to true on successful connection
}

QString WebSocketService::protocol_string() const {
    return "WebSocket";
}

Network::Protocol WebSocketService::protocol() const {
    return Network::Protocol::WebSocket;
}

void WebSocketService::closeSocket() {
    QMutexLocker locker(&m_stateMutex);

    activated_ = false;
    waiting_buffer_space_ = false;
    closed_ = true;
    m_socketValid.store(false);

           // Clear all queues
    {
        QMutexLocker queueLocker(&queue_mutex_);
        std::queue<QByteArray> empty1, empty2, empty3, empty4;
        high_queue_.swap(empty1);
        normal_queue_.swap(empty2);
        low_queue_.swap(empty3);
        m_messageCache.swap(empty4);
    }

           // Stop ping timer
    if (m_pingTimer) {
        m_pingTimer->stop();
        m_pingTimer->deleteLater();
        m_pingTimer = nullptr;
    }

           // Close socket safely
    if (m_ws) {
        if (m_ws->state() == QAbstractSocket::ConnectedState) {
            eLog("[WS] Closing socket");
            m_ws->close(QWebSocketProtocol::CloseCodeNormal);
        }

        cleanupSocket();
    }

           // Emit disconnected signal if not already done
    if (!is_disconnected_) {
        is_disconnected_ = true;
        locker.unlock(); // Unlock before emitting signal
        emit disconnected();
    }
}

void WebSocketService::cleanupSocket() {
    if (m_ws) {
        m_ws->disconnect(); // Disconnect all signals
        m_ws->deleteLater();
        m_ws = nullptr;
    }
}

bool WebSocketService::operator==(const WebSocketService &service) const {
    QMutexLocker locker(&m_stateMutex);
    return m_ws == service.m_ws;
}

void WebSocketService::onTextMessage(const QString &message) {
    m_failedPongs.store(0);

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

        pub_ = KeyPublic(ByteArray(pub_result.value()).toArray<crypto_sign_PUBLICKEYBYTES>());
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
    m_failedPongs.store(0);

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
    if (!isSocketValid()) {
        return false;
    }

    QMutexLocker locker(&m_stateMutex);
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

    QByteArray data;
    {
        QMutexLocker locker(&queue_mutex_);
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
    }

    if (!data.isEmpty()) {
        emit sendMessageInternal(data);

               // Check if more messages to send
        QMutexLocker locker(&queue_mutex_);
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

    if (!isSocketValid()) {
        return;
    }

    bool sent = safeSocketSend([this, &prepared]() {
        return m_ws->sendBinaryMessage(prepared);
    }, "binary message");

    if (!sent) {
        eCritical("[WS] Failed to send message");
        emit error(Network::SocketServiceError::CantSend, "", ip_.toStdString(), identifier_.toStdString());
    }
}

void WebSocketService::flush() {
    if (!isSocketValid() || !activated_) {
        return;
    }

    QMutexLocker locker(&m_stateMutex);
    if (m_ws->bytesToWrite() == 0) {
        return;
    }

    try {
        m_ws->flush();
    } catch (const std::exception &e) {
        eLog("[WS] Flush exception: {}", e.what());
        QTimer::singleShot(0, this, &WebSocketService::closeSocket);
    } catch (...) {
        eLog("[WS] Unknown flush exception");
        QTimer::singleShot(0, this, &WebSocketService::closeSocket);
    }
}

void WebSocketService::onConnected() {
    QMutexLocker locker(&m_stateMutex);
    this->ip_ = m_ws->peerAddress().toString().replace("::ffff:", "");
    this->port_ = m_ws->peerPort();
    m_socketValid.store(true);
    locker.unlock();

    send_public_key();
    eLog("[WS] New service connected: {} {}", ip_, port());
}

void WebSocketService::onSocketError(QAbstractSocket::SocketError error) {
    eLog("[WS] Socket error: {}, {}", Utils::enum_value_name(error), ip_);
    m_socketValid.store(false);
    closeSocket();
}

void WebSocketService::connections() {
    if (!m_ws) return;

    connect(m_ws, &QWebSocket::connected, this, &WebSocketService::onConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &WebSocketService::closeSocket);
    connect(this, &WebSocketService::closeSocketSig, this, &WebSocketService::closeSocket);

    connect(m_ws, &QWebSocket::textMessageReceived, this, &WebSocketService::onTextMessage, Qt::QueuedConnection);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &WebSocketService::onBinaryMessage, Qt::QueuedConnection);

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

    if (!isSocketValid()) {
        closeSocket();
        return;
    }

    bool sent = safeSocketSend([this, &pub_key_str]() {
        return m_ws->sendTextMessage(QString::fromStdString(pub_key_str));
    }, "public key");

    if (!sent) {
        eCritical("[WS] Handshake send failed");
        emit error(Network::SocketServiceError::IncorrectHandshake,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
    }
}

void WebSocketService::handshake() {
    auto first_message = generate_first_message();
    auto encrypted = prepareSendMessage(first_message);
    if (encrypted.isEmpty()) {
        emit error(Network::SocketServiceError::IncorrectHandshake,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
        return;
    }

    auto encoded_json = Utils::to_base64(encrypted.toStdString());

    if (!isSocketValid()) {
        closeSocket();
        return;
    }

    bool sent = safeSocketSend([this, &encoded_json]() {
        return m_ws->sendTextMessage(QString::fromStdString(encoded_json));
    }, "handshake");

    if (!sent) {
        eCritical("[WS] Handshake send failed");
        emit error(Network::SocketServiceError::IncorrectHandshake,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString());
    }
}

quint16 WebSocketService::port() const {
    QMutexLocker locker(&m_stateMutex);
    if (!m_ws) {
        return 0;
    }

    if (m_ws->peerPort() != node->network()->wsPort) {
        return m_ws->peerPort();
    } else {
        return m_ws->localPort();
    }
}

quint16 WebSocketService::server_port() const {
    return node->network()->wsPort;
}

void WebSocketService::processCachedMessages() {
    QMutexLocker locker(&queue_mutex_);
    while (!m_messageCache.empty()) {
        eLog("Processing cached message");
        auto message = m_messageCache.front();
        m_messageCache.pop();
        locker.unlock();

        processMessage(message);

        locker.relock();
    }
}
