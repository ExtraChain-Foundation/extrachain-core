#include "network/websocket_service.h"

#include "dfs/managers/headers/dfs_networkmanager.h"

#ifndef EXTRACHAIN_CMAKE
#include "preconfig.h"
#endif

WebSocketService::WebSocketService(QWebSocket *ws, NetworkManager *networkManager, QObject *parent)
    : SocketService(networkManager, parent)
{
    if (ws == nullptr)
    {
        m_ws = new QWebSocket("ExtraChain " + QString(EXTRACHAIN_VERSION));
        qDebug() << "[WS] Create new ws";
    }
    else
    {
        m_ws = ws;
        this->m_ip = m_ws->localAddress().toString().replace("::ffff:", "");
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
    return m_ws->isValid() && m_activated;
}

void WebSocketService::open(const QUrl &url)
{
    if (m_ws->isValid())
    {
        qFatal("[WS] Already opened");
    }
    else
    {
        qDebug() << "[WS] Open" << url;
        connections();
        m_ws->open(url);
        m_ip = m_ws->localAddress().toString();
    }
}

void WebSocketService::closeSocket()
{
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
    qDebug() << "[WS] First message:" << message;

    if (!m_activated)
    {
        m_activated = checkFirstMessage(message);
    }
    else
    {
        qFatal("[WS] Extra text message");
    }
}

void WebSocketService::onBinaryMessage(const QByteArray &message)
{
    if (!m_activated)
        qFatal("[WS] Binary: not activated");

    // qDebug() << "[WS] Binary length:" << message.length();
    // qDebug() << "[WS] Binary:" << message;

    SocketPair pair(m_ip.toStdString(), port());
    pair.setIdentifier(m_identifier.toLatin1());
    auto mess = qUncompress(message);
    m_bytesCompressed += mess.length() - message.length();
    m_bytesIncoming += message.length();
    m_networkManager->messageReceived(mess, pair);
}

void WebSocketService::sendMessage(const QByteArray &data)
{
    if (!isActive())
    {
        qDebug() << (QString("[WS] Try to send without activation %1").arg(QString(data)).toUtf8().data());
        return;
    }
    if (!data.length())
        qFatal("[WS] Error send size");

    auto compress = qCompress(data);
    m_bytesCompressed += data.length() - compress.length();
    m_bytesOutgoing += compress.length();

    auto length = m_ws->sendBinaryMessage(compress);
    Q_UNUSED(length)
    // m_ws->flush();

    if (m_ws->isValid())
    {
        // qDebug().noquote() << "[WS] Send" << data.length() << length << m_ws->isValid() << m_ip
        //                    << port();
        // qDebug() << "[WS] Send" << m_ws << data << length;
    }
    else
    {
        qFatal("[WS] Can't send");
    }
}

void WebSocketService::onSocketError(QAbstractSocket::SocketError error)
{ // maybe move
    qDebug() << "[WS] Socket error:" << error;

    if (m_ws->state() != QAbstractSocket::ConnectedState)
        closeSocket();
}

void WebSocketService::connections()
{
    connect(m_ws, &QWebSocket::connected, [this] {
        this->m_ip = m_ws->localAddress().toString().replace("::ffff:", "");
        qDebug() << "[WS] New service:" << m_ip << port();
        emit m_networkManager->newSocket();
        sendFirstMessage();
    });
    connect(m_ws, &QWebSocket::disconnected, [this] {
        qDebug() << "[WS] Disconnected";
        closeSocket();
    });
    connect(m_ws, &QWebSocket::textMessageReceived, this, &WebSocketService::onTextMessage);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &WebSocketService::onBinaryMessage);
    connect(this, &WebSocketService::send, this, &WebSocketService::sendMessage);
    connect(this, &WebSocketService::close, this, &WebSocketService::closeSocket);
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
    if (m_ws->peerPort() == m_networkManager->wsPort)
        return m_ws->peerPort();
    else
        return m_ws->localPort();
}
