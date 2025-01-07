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

#include <QJsonObject>

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
        m_ws         = ws;
        this->m_ip   = m_ws->peerAddress().toString().replace("::ffff:", "");
        this->m_port = m_ws->peerPort();
        eLog("[WS] New service: {}", m_ip);
        connections();
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
    m_ws->deleteLater();
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
    m_waitingForBufferSpace = false;
    m_activated             = false;

    {
        QMutexLocker locker(&m_queueMutex);
        m_highQueue.clear();
        m_normalQueue.clear();
        m_lowQueue.clear();
        m_messageCache.clear();
    }

    eLog("[WS] Close socket");
    if (m_ws && m_ws->state() == QAbstractSocket::ConnectedState) {
        m_ws->close();
    }

    m_activated = false;
    emit disconnected();
}

bool WebSocketService::operator==(const WebSocketService &service) const {
    return m_ws == service.m_ws;
}

void WebSocketService::onTextMessage(const QString &message) // for first message
{
    if (message.isEmpty())
        return;

    auto json = QJsonDocument::fromJson(message.toLatin1());

    if (json["isRequest"].toBool()) {
        auto json    = QJsonDocument::fromJson(message.toLatin1());
        m_isConstant = json["isConstant"].toBool();
        m_identifier = json["identifier"].toString();
        auto tempPub = json["pub"].toString();

        eInfo("[WS] First message key achieved: {}, isConstant: {}",
              json.toJson(QJsonDocument::Compact),
              m_isConstant);

        pub = KeyPublic(ByteArray::fromBase64(tempPub).toArray<crypto_sign_PUBLICKEYBYTES>());
        if (pub.empty()) { // or incorrect
            eFatal("Incorrect public key in socket");
        }

        QJsonObject jsonAnswer;
        jsonAnswer["isRequest"]        = false;
        jsonAnswer["canUseConnection"] = !m_needToDelete;
        jsonAnswer["pub"]              = ByteArray(priv.public_key()).toBase64QString();

        auto firstMessage  = generateFirstMessage();
        auto prepared      = prepareSendMessage(firstMessage);
        jsonAnswer["data"] = ByteArray(prepared).toBase64QString();

        QByteArray result = QJsonDocument(jsonAnswer).toJson(QJsonDocument::JsonFormat::Compact);

        m_activated = true;
        processCachedMessages();
        m_timer.setSingleShot(true);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            eLog("[Socket] Emit activation after timeout: {} {} {}", fmt::ptr(this), ip(), protocol());
            emit activated();
        });
        m_timer.start(1000);

        if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
            closeSocket();
            return;
        }

        qint64 written = m_ws->sendTextMessage(result);
        if (written < 0) {
            eCritical("[WS] Handshake send failed");
            closeSocket();
            return;
        }

        if (m_ws->bytesToWrite() > 0) {
            m_ws->flush();
        }

        if (m_needToDelete)
            closeSocket();
        return;
    }

    if (m_activated)
        return;

    bool canUseConnection = json["canUseConnection"].toBool();

    auto tempPub = json["pub"].toString();
    pub          = KeyPublic(ByteArray::fromBase64(tempPub).toArray<crypto_sign_PUBLICKEYBYTES>());

    if (pub.empty()) {
        eLog("Incorrect public key in socket");
        emit error(Network::SocketServiceError::IncorrectPublicKey, "");
        closeSocket();
        return;
    }

    auto data = json["data"].toString();
    eLog("[WS] First message: {}", data);
    auto coded   = ByteArray::fromBase64(data).toQByteArray();
    auto decoded = prepareReceiveMessage(coded);
    checkFirstMessage(decoded, canUseConnection);

    if (m_needToDelete)
        closeSocket();
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
    this->m_ip   = m_ws->peerAddress().toString().replace("::ffff:", "");
    this->m_port = m_ws->peerPort();
    handshake();
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

void WebSocketService::handshake() {
    QJsonObject json;
    json["isRequest"]  = true;
    json["isConstant"] = m_isConstant.load();
    json["pub"]        = ByteArray(priv.public_key()).toBase64QString();
    auto ident         = QString(Network::currentIdentifier());
    json["identifier"] = ident;

    QByteArray result = QJsonDocument(json).toJson(QJsonDocument::JsonFormat::Compact);

    if (!m_ws || !m_ws->isValid() || m_ws->state() != QAbstractSocket::ConnectedState) {
        closeSocket();
        return;
    }

    qint64 written = m_ws->sendTextMessage(result);
    if (written < 0 || m_ws->error() != QAbstractSocket::UnknownSocketError) {
        eCritical("[WS] Handshake send failed");
        closeSocket();
        return;
    }

    if (m_ws->bytesToWrite() > 0) {
        m_ws->flush();
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
