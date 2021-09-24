#include "network/websocket_service.h"

#include "dfs/managers/headers/dfs_networkmanager.h"

WebSocketService::WebSocketService(QWebSocket *ws, NetworkManager *networkManager, QObject *parent)
    : SocketService(networkManager, parent)
{
    if (ws == nullptr)
    {
        m_ws = new QWebSocket("ExtraChain");
        qDebug() << "[WS] Create new ws";
    }
    else
    {
        m_ws = ws;
        this->m_ip = m_ws->peerAddress().toString().replace("::ffff:", "");
        qDebug() << "[WS] New service:" << m_ip << port();
        connections();
        sendFirstMessage();
    }
}

WebSocketService::WebSocketService(const WebSocketService &service)
    : SocketService(service)
{
    qFatal("[WS] Copy");
    this->m_ws = service.m_ws;
}

WebSocketService::~WebSocketService()
{
    qDebug() << "[WS] I'm socket, i'm death";
    m_ws->deleteLater();
}

QWebSocket *WebSocketService::socket() const
{
    return m_ws;
}

bool WebSocketService::isActive() const
{
    return m_activated && m_ws->isValid();
}

void WebSocketService::open(const QString &ip, quint16 port)
{
    if (m_ws->isValid())
    {
        qFatal("[WS] Already opened");
    }
    else
    {
        auto url = QUrl(QString("ws://%1:%2").arg(ip).arg(port));
        qDebug() << "[WS] Open" << url;
        connections();
        m_ws->open(url);
        m_ip = m_ws->peerAddress().toString();
    }
}

void WebSocketService::closeSocket()
{
    qDebug() << "[WS] Close socket";
    m_activated = false;
    m_ws->close();
    emit disconnected();
}

bool WebSocketService::operator==(const WebSocketService &service) const
{
    return m_ws == service.m_ws;
}

void WebSocketService::onTextMessage(const QString &message) // for first message
{
    if (m_activated)
        return;

    qDebug() << "[WS] First message:" << message;
    m_activated = checkFirstMessage(message);
    if (m_activated)
        emit activated();
}

void WebSocketService::onBinaryMessage(const QByteArray &message)
{
    if (!m_activated)
        qFatal("[WS] Binary: not activated");

    auto mess = prepareReceiveMessage(message);
    if (!mess.isEmpty())
    {
        SocketPair pair(m_ip.toStdString(), port());
        pair.setIdentifier(m_identifier.toLatin1());
        m_networkManager->messageReceived(mess, pair);
    }
    else
    {
        qFatal("[WS] Messsage is empty after prepare");
    }
}

void WebSocketService::sendMessage(const QByteArray &data)
{
    if (!isActive())
    {
        qDebug() << "[WS] Try to send without activation" << data.left(35);
        return;
    }
    if (data.isEmpty())
        qFatal("[WS] Error send size");

    m_ws->sendBinaryMessage(prepareSendMessage(data));
    // m_ws->flush();
}

void WebSocketService::onConnected()
{
    this->m_ip = m_ws->peerAddress().toString().replace("::ffff:", "");
    qDebug() << "[WS] New service:" << m_ip << port();
    emit m_networkManager->newSocket();
    sendFirstMessage();
}

void WebSocketService::onSocketError(QAbstractSocket::SocketError error)
{
    qDebug() << "[WS] Socket error:" << error;

    if (m_ws->state() != QAbstractSocket::ConnectedState)
        closeSocket();
}

void WebSocketService::connections()
{
    connect(m_ws, &QWebSocket::connected, this, &WebSocketService::onConnected);
    connect(m_ws, &QWebSocket::disconnected, this, &WebSocketService::closeSocket);
    connect(m_ws, &QWebSocket::textMessageReceived, this, &WebSocketService::onTextMessage);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &WebSocketService::onBinaryMessage);
    connect(this, &WebSocketService::send, this, &WebSocketService::sendMessage);
    connect(this, &WebSocketService::close, this, &WebSocketService::closeSocket); // slot
    connect(m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
            &WebSocketService::onSocketError);
}

void WebSocketService::sendFirstMessage()
{
    auto json = generateFirstMessage();
    m_ws->sendTextMessage(json);
}

quint16 WebSocketService::port() const
{
    if (m_ws->peerPort() != m_networkManager->wsPort)
        return m_ws->peerPort();
    else
        return m_ws->localPort();
}

quint16 WebSocketService::serverPort() const
{
    return m_networkManager->wsPort;
}
