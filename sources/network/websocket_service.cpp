#include "network/websocket_service.h"

WebSocketService::WebSocketService(
    QWebSocket     *ws,
    ExtraChainNode *node,
    QObject        *parent,
    const bool      isConstant,
    const bool      needToDelete)
    : SocketService(node, parent) {
    m_isConstant = isConstant;
    m_needToDelete = needToDelete;
    if (ws == nullptr) {
        m_ws = new QWebSocket("ExtraChain");
        qDebug() << "[WS] Create new ws";
    } else {
        m_ws         = ws;
        this->m_ip   = m_ws->peerAddress().toString().replace("::ffff:", "");
        this->m_port = m_ws->peerPort();
        qDebug() << "[WS] New service:" << m_ip;
        connections();
    }

    connect(this, &WebSocketService::sendMessageInternal, this, &WebSocketService::sendMessageInternalSlot);
}

WebSocketService::~WebSocketService() {
    qDebug() << "[WS] I'm socket, i'm death";
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
        qFatal("[WS] Already opened");
    } else {
        auto url = QUrl(QString("ws://%1:%2").arg(ip).arg(port));
        qDebug() << "[WS] Open" << url;
        connections();
        m_ws->open(url);
        m_ip   = m_ws->peerAddress().toString();
        m_port = m_ws->peerPort();
    }
}

void WebSocketService::closeSocket() {
    qDebug() << "[WS] Close socket";
    m_activated = false;
    if (m_ws->isValid())
        m_ws->close();
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

        qInfo() << QString("[WS] First message key achieved: %1, isConstant: %2")
                       .arg(json.toJson())
                       .arg(m_isConstant);

        pub = KeyPublic(ByteArray::fromBase64(tempPub).toArray<32>());
        if (pub.empty()) { // or incorrect
            qFatal("Incorrect public key in socket");
        }

        QJsonObject jsonAnswer;
        jsonAnswer["isRequest"]        = false;
        jsonAnswer["canUseConnection"] = !m_needToDelete;
        jsonAnswer["pub"]              = ByteArray(priv.publicKey()).toBase64QString();

        auto firstMessage  = generateFirstMessage();
        auto prepared     = prepareSendMessage(firstMessage);
        jsonAnswer["data"] = ByteArray(prepared).toBase64QString();

        QByteArray result = QJsonDocument(jsonAnswer).toJson(QJsonDocument::JsonFormat::Compact);

        m_activated = true;
        QTimer::singleShot(1000, [this] {
            qDebug() << "[Socket] Emit activation after timeout:" << this << ip() << protocol();
            emit activated();
        });

        m_ws->sendTextMessage(result);
        m_ws->flush();
        if (m_needToDelete)
            closeSocket();
        return;
    }

    if (m_activated)
        return;

    bool canUseConnection = json["canUseConnection"].toBool();

    auto tempPub = json["pub"].toString();
    pub          = KeyPublic(ByteArray::fromBase64(tempPub).toArray<32>());
    if (pub.empty()) {
        qDebug("Incorrect public key in socket");
        emit error(Network::SocketServiceError::IncorrectPublicKey, "");
        closeSocket();
        return;
    }

    auto data = json["data"].toString();
    qDebug() << "[WS] First message:" << data;
    auto coded   = ByteArray::fromBase64(data).toQByteArray();
    auto decoded = prepareReceiveMessage(coded);
    checkFirstMessage(decoded, canUseConnection);

    if (m_needToDelete)
        closeSocket();
}

void WebSocketService::onBinaryMessage(const QByteArray &message) {
    if (!m_activated)
        qFatal("[WS] Binary: not activated");

    auto mess = prepareReceiveMessage(message);
    if (!mess.isEmpty()) {
        node->network()->messageReceived(mess.toStdString(), m_ip.toStdString(), m_identifier.toStdString());
    } else {
        qFatal("[WS] Messsage is empty after prepare");
    }
}

void WebSocketService::sendMessage(const QByteArray &data) {
    if (!isActive()) {
        qDebug() << "[WS] Try to send without activation" << data.left(35);
        return;
    }
    if (data.isEmpty())
        qFatal("[WS] Error send size");

    emit sendMessageInternal(data);
}

void WebSocketService::sendMessageInternalSlot(const QByteArray &data) {
    m_ws->sendBinaryMessage(prepareSendMessage(data));
}

void WebSocketService::final() {
    if (this->m_activated && m_ws->isValid() && m_ws->bytesToWrite() > 0)
        m_ws->flush();
}

void WebSocketService::onConnected() {
    this->m_ip   = m_ws->peerAddress().toString().replace("::ffff:", "");
    this->m_port = m_ws->peerPort();
    handshake();
    qDebug() << "[WS] New service:" << m_ip << port();
}

void WebSocketService::onSocketError(QAbstractSocket::SocketError error) {
    qDebug() << "[WS] Socket error:" << error;

    if (m_ws->state() != QAbstractSocket::ConnectedState)
        closeSocket();
}

void WebSocketService::connections() {
    connect(m_ws, &QWebSocket::connected, this, &WebSocketService::onConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &WebSocketService::closeSocket);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &WebSocketService::onTextMessage);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &WebSocketService::onBinaryMessage);
    // connect(this, &WebSocketService::send, this, &WebSocketService::sendMessage);
    connect(this, &WebSocketService::close, this, &WebSocketService::closeSocket); // slot
    connect(m_ws, &QWebSocket::errorOccurred, this, &WebSocketService::onSocketError);
}

void WebSocketService::handshake() {
    QJsonObject json;
    json["isRequest"]  = true;
    json["isConstant"] = m_isConstant.load();
    json["pub"]        = ByteArray(priv.publicKey()).toBase64QString();
    json["identifier"] = QString(Network::currentIdentifier());

    QByteArray result = QJsonDocument(json).toJson(QJsonDocument::JsonFormat::Compact);
    m_ws->sendTextMessage(result);
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
