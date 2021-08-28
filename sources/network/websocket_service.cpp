#include "network/websocket_service.h"

#include "dfs/managers/headers/dfsnetmanager.h"

#ifndef EXTRACHAIN_CMAKE
#include "preconfig.h"
#endif

QString networkName = "ExtraChain"; // temp

WebSocketService::WebSocketService(QWebSocket *ws, QObject *parent)
    : QObject(parent)
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
        qDebug() << "[WS] New service:" << m_ip << m_ws->localPort();
        connections();
        sendFirstMessage();
    }
}

WebSocketService::WebSocketService(const WebSocketService &service)
{
    this->m_ws = service.m_ws;
}

WebSocketService::~WebSocketService()
{
    qDebug() << "[WS] I'm socket, i'm death";
    m_ws->deleteLater();
}

QWebSocket *WebSocketService::ws() const
{
    return m_ws;
}

const QString &WebSocketService::identificator() const
{
    return m_identificator;
}

bool WebSocketService::isActive() const
{
    return m_ws->isValid() && activated;
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
    }
}

void WebSocketService::close()
{
    m_ws->close();
    emit disconnected();
}

void WebSocketService::sendError(int code, const QString &text)
{
    qDebug() << "[WS] Error" << code << text;
    m_ws->sendTextMessage(QString("{\"error\":%1,\"errorText\":\"%2\"}").arg(code).arg(text));
    emit error(code, text);
}

bool WebSocketService::operator==(const WebSocketService &service) const
{
    return m_ws == service.m_ws;
}

void WebSocketService::onTextMessage(const QString &message) // for first message and errors
{
    qDebug() << "[WS] Text:" << message;

    if (message.left(8) == "{\"error\"")
    {
        parseError(message);
    }

    if (!activated)
    {
        auto json = QJsonDocument::fromJson(message.toLatin1());
        auto network = json["network"].toString();
        auto version = json["version"].toString();

        if (network != networkName || version != EXTRACHAIN_VERSION)
        {
            qDebug() << "[WS] Close, because network or version unsuitable";
            network != networkName ? sendError(1, "Not suitable version")
                                   : sendError(2, "Not suitable version");
            close();
        }

        m_identificator = json["identificator"].toString();
        if (m_identificator == net::readNetManagerIdentificator())
        {
            sendError(3, "Not suitable identificator");
            close();
        }
        activated = true;

        qDebug() << "[WS] Activated" << this << m_ws->localPort();
    }
}

void WebSocketService::onBinaryMessage(const QByteArray &message)
{
    if (!activated)
        qFatal("[WS] Binary: not activated");

    qDebug() << "[WS] Binary length:" << message.length();
    // qDebug() << "[WS] Binary:" << message;

    SocketPair pair(m_ip.toStdString(), m_ws->localPort());
    pair.setId(m_identificator.toLatin1());
    auto mess = qUncompress(message);
    if (m_ws->localPort() == 2234)
    {
        reinterpret_cast<DFSNetManager *>(networkManager)->MessageReceived(mess, pair);
    }
    else
    {
        networkManager->MessageReceived(mess, pair);
    }
    // emit resolveMessage(qUncompress(message), pair);
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

    static int tempSizeDiff = 0;
    auto compress = qCompress(data);
    tempSizeDiff += data.length() - compress.length();
    // qDebug() << "[WS] Size diff:" << tempSizeDiff;
    auto length = m_ws->sendBinaryMessage(compress);
    // m_ws->flush();

    if (m_ws->isValid())
    {
        // qDebug().noquote() << "[WS] Send" << data.length() << length << m_ws->isValid() << m_ip
        //                    << m_ws->localPort();
        // qDebug() << "[WS] Send" << m_ws << data << length;
    }
    else
    {
        qDebug() << "[WS] Cant send" << m_ws << m_ws->localPort();
    }
}

void WebSocketService::onError(QAbstractSocket::SocketError error)
{
    qDebug() << "[WS] Socket error:" << m_ws->errorString() << error;

    if (m_ws->state() != QAbstractSocket::ConnectedState)
        emit disconnected();
}

void WebSocketService::connections()
{
    connect(m_ws, &QWebSocket::connected, [this] {
        this->m_ip = m_ws->localAddress().toString().replace("::ffff:", "");
        qDebug() << "[WS] New service:" << m_ip << m_ws->localPort();

        sendFirstMessage();
    });
    connect(m_ws, &QWebSocket::disconnected, [this] {
        qDebug() << "[WS] Disconnected";
        emit disconnected();
    });
    connect(m_ws, &QWebSocket::textMessageReceived, this, &WebSocketService::onTextMessage);
    connect(m_ws, &QWebSocket::binaryMessageReceived, this, &WebSocketService::onBinaryMessage);
    connect(this, &WebSocketService::send, this, &WebSocketService::sendMessage);
    connect(m_ws, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::error), this,
            &WebSocketService::onError);
}

void WebSocketService::sendFirstMessage()
{
    qDebug() << "[WS] Send first message" << m_ws->localPort();
    QJsonObject json;
    json["network"] = networkName;
    json["version"] = EXTRACHAIN_VERSION;
    json["identificator"] = QString(net::readNetManagerIdentificator());
    // company id
    m_ws->sendTextMessage(QJsonDocument(json).toJson(QJsonDocument::JsonFormat::Compact));
}

void WebSocketService::parseError(const QString &message)
{
    auto json = QJsonDocument::fromJson(message.toLatin1());
    int code = json["error"].toInt();
    QString text = json["errorText"].toString();
    qDebug() << "[WS] Error (parsed)" << code << text;
    emit error(code, text);
}

const QString &WebSocketService::ip() const
{
    return m_ip;
}

void WebSocketService::setNetworkManager(NetManager *newNetworkManager)
{
    networkManager = newNetworkManager;
}
