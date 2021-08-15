#include "network/websocket_service.h"

#include "utils/exc_utils.h"

WebSocketService::WebSocketService(QWebSocket *ws, QObject *parent)
    : QObject(parent)
{
    m_ws = ws;
    qDebug() << "[WS] New service:" << m_ws->localAddress().toString() << m_ws->localPort();

    connect(m_ws, &QWebSocket::textMessageReceived, this, &WebSocketService::onTextMessage);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &WebSocketService::onBinaryMessage);
    connect(this, &WebSocketService::send, this, &WebSocketService::onSend);

    QJsonObject json;
    json["network"] = "ExtraChain";
    json["version"] = EVERSION;
    json["identificator"] = QString(net::readNetManagerIdentificator());
    onSend(QJsonDocument(json).toJson(QJsonDocument::JsonFormat::Compact));
}

WebSocketService::WebSocketService(const WebSocketService &service)
{
    this->m_ws = service.m_ws;
}

WebSocketService::~WebSocketService()
{
    qFatal("destroy");
    // m_ws->deleteLater();
}

QWebSocket *WebSocketService::ws() const
{
    return m_ws;
}

bool WebSocketService::operator==(const WebSocketService &service)
{
    return m_ws == service.m_ws;
}

void WebSocketService::onTextMessage(const QString &message)
{
    qDebug() << "[WS] Message:" << message;
    emit send(message.toUtf8());
}

void WebSocketService::onBinaryMessage(const QByteArray &message)
{
    qDebug() << "[WS] Binary:" << message;
}

void WebSocketService::onSend(const QByteArray &data)
{
    qDebug() << "[WS] Send" << m_ws << data << m_ws->sendTextMessage(data);
}
