#include "network/websocket_service.h"

WebSocketService::WebSocketService(QWebSocket *ws, QObject *parent)
    : QObject(parent)
{
    m_ws = ws;
    qDebug() << "[WS] New service:" << m_ws->localAddress().toString() << m_ws->localPort();

    connect(m_ws, &QWebSocket::textMessageReceived,
            [](const QString &message) { qDebug() << "[WS] Message:" << message; });
    connect(m_ws, &QWebSocket::binaryMessageReceived,
            [](const QByteArray &message) { qDebug() << "[WS] Binary:" << message; });
    connect(m_ws, &QWebSocket::disconnected, []() { qDebug() << "[WS] Disconnected"; });
}

WebSocketService::WebSocketService(const WebSocketService &service)
{
    this->m_ws = service.m_ws;
}

// WebSocketService::~WebSocketService()
//{
//    m_ws->deleteLater();
//}

QWebSocket *WebSocketService::ws() const
{
    return m_ws;
}

void WebSocketService::send(const QByteArray &data)
{
    qDebug() << m_ws << data << m_ws->sendTextMessage(data);
}
