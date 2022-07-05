#include "network/websocket_service.h"

WebSocketService::WebSocketService(QWebSocket *ws, ExtraChainNode &node, QObject *parent)
    : SocketService(node, parent) {
    if (ws == nullptr) {
        m_ws = new QWebSocket("ExtraChain");
        qDebug() << "[WS] Create new ws";
    } else {
        m_ws = ws;
        this->m_ip = m_ws->peerAddress().toString().replace("::ffff:", "");
        qDebug() << "[WS] New service:" << m_ip;
        connections();
        handshake();
    }
}

// WebSocketService::WebSocketService(const WebSocketService &service)
//     : SocketService(service) {
//     qFatal("[WS] Copy");
//     this->m_ws = service.m_ws;
// }

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
        m_ip = m_ws->peerAddress().toString();
    }
}

void WebSocketService::closeSocket() {
    qDebug() << "[WS] Close socket";
    m_activated = false;
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

    if (pub.empty()) {
        if (message == "StatusOnly") {
            m_ws->sendTextMessage(generateFirstMessage());
            return;
        }

        auto key = Utils::bytesDecode(message.toLatin1());
        pub = KeyPublic(key.toStdString());
        if (pub.empty()) { // or incorrect
            qFatal("Incorrect public key in socket");
        }

        auto firstMessage = Utils::bytesEncode(prepareSendMessage(generateFirstMessage()));
        m_ws->sendTextMessage(firstMessage);
        return;
    }

    if (m_activated)
        return;

    qDebug() << "[WS] First message:" << message;
    checkFirstMessage(prepareReceiveMessage(Utils::bytesDecode(message.toLatin1())));
}

void WebSocketService::onBinaryMessage(const QByteArray &message) {
    if (!m_activated)
        qFatal("[WS] Binary: not activated");

    auto mess = prepareReceiveMessage(message);
    if (!mess.isEmpty()) {
        node.network()->messageReceived(mess.toStdString(), m_identifier.toStdString());
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

    m_ws->sendBinaryMessage(prepareSendMessage(data));
    // m_ws->flush();
}

void WebSocketService::onConnected() {
    this->m_ip = m_ws->peerAddress().toString().replace("::ffff:", "");
    handshake();
    qDebug() << "[WS] New service:" << m_ip << port();
    emit node.network()->newSocket();
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
    //    connect(this, &WebSocketService::send, this, &WebSocketService::sendMessage);
    connect(this, &WebSocketService::close, this, &WebSocketService::closeSocket); // slot
    connect(m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
            &WebSocketService::onSocketError);
}

void WebSocketService::handshake() {
    auto key = Utils::bytesEncode(QByteArray::fromStdString(priv.publicKey()));
    m_ws->sendTextMessage(key);
}

quint16 WebSocketService::port() const {
    if (m_ws->peerPort() != node.network()->wsPort)
        return m_ws->peerPort();
    else
        return m_ws->localPort();
}

quint16 WebSocketService::serverPort() const {
    return node.network()->wsPort;
}
