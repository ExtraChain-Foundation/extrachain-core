#include "network/websocket_service.h"

#include "dfs/managers/headers/dfsnetmanager.h"

#ifndef EXTRACHAIN_CMAKE
#include "preconfig.h"
#endif

WebSocketService::WebSocketService(QWebSocket *ws, NetManager *newNetworkManager, ActorIndex *newActorIndex,
                                   QObject *parent)
    : QObject(parent)
{
    networkManager = newNetworkManager;
    actorIndex = newActorIndex;

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
{
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

const QString &WebSocketService::identifier() const
{
    return m_identifier;
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
        m_ip = m_ws->localAddress().toString();
    }
}

void WebSocketService::closeSocket()
{
    m_ws->close();
    emit disconnected();
}

void WebSocketService::sendError(Network::SocketServiceError code)
{
    qDebug() << "[WS] Error" << int(code);
    m_ws->sendTextMessage(QString("{\"error\":%1\"}").arg(int(code)));
    emit error(code);
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
        auto version = json["version"].toString();
        ActorId jsonFirstId = ActorId(json["firstId"].toString().toLatin1());
        ActorId currentFirstId = actorIndex->firstId();
        bool isFirstIdsContains = currentFirstId == jsonFirstId.toByteArray();
        bool somethingEmpty = jsonFirstId.isEmpty() || currentFirstId.isEmpty();

        qDebug() << "[WS]" << currentFirstId << jsonFirstId << currentFirstId.isEmpty()
                 << jsonFirstId.isEmpty() << isFirstIdsContains << somethingEmpty
                 << (version != EXTRACHAIN_VERSION);

        if (!(somethingEmpty || isFirstIdsContains) || version != EXTRACHAIN_VERSION)
        {
            qDebug() << "[WS] Close, because network or version unsuitable";
            version != EXTRACHAIN_VERSION ? sendError(Network::SocketServiceError::IncompatibleVersion)
                                          : sendError(Network::SocketServiceError::IncompatibleNetwork);
            // emit incorrectSocket
            closeSocket();
            return;
        }

        m_identifier = json["identifier"].toString();
        if (m_identifier == net::readNetManagerIdentifier())
        {
            sendError(Network::SocketServiceError::IncompatibleIdentifier);
            closeSocket();
            return;
        }

        activated = true;
        qDebug() << "[WS] Activated" << this << port();
    }
}

void WebSocketService::onBinaryMessage(const QByteArray &message)
{
    if (!activated)
        qFatal("[WS] Binary: not activated");

    // qDebug() << "[WS] Binary length:" << message.length();
    // qDebug() << "[WS] Binary:" << message;

    SocketPair pair(m_ip.toStdString(), port());
    pair.setIdentifier(m_identifier.toLatin1());
    auto mess = qUncompress(message);
    if (m_ws->localPort() == 2234)
    {
        reinterpret_cast<DFSNetManager *>(networkManager)->MessageReceived(mess, pair);
    }
    else
    {
        networkManager->MessageReceived(mess, pair);
    }
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
        qDebug() << "[WS] Cant send" << m_ws << port();
    }
}

void WebSocketService::onSocketError(QAbstractSocket::SocketError error)
{
    qDebug() << "[WS] Socket error:" << m_ws->errorString() << error;

    if (m_ws->state() != QAbstractSocket::ConnectedState)
        emit disconnected();
}

void WebSocketService::connections()
{
    connect(m_ws, &QWebSocket::connected, [this] {
        this->m_ip = m_ws->localAddress().toString().replace("::ffff:", "");
        qDebug() << "[WS] New service:" << m_ip << port();
        emit networkManager->newSocket();

        sendFirstMessage();
    });
    connect(m_ws, &QWebSocket::disconnected, [this] {
        qDebug() << "[WS] Disconnected";
        emit disconnected();
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
    qDebug() << "[WS] Send first message" << port();
    QJsonObject json;
    json["firstId"] = actorIndex->firstId().toString();
    json["version"] = EXTRACHAIN_VERSION;
    json["identifier"] = QString(net::readNetManagerIdentifier());
    QByteArray jsonBytes = QJsonDocument(json).toJson(QJsonDocument::JsonFormat::Compact);
    m_ws->sendTextMessage(jsonBytes);
}

void WebSocketService::parseError(const QString &message)
{
    auto json = QJsonDocument::fromJson(message.toLatin1());
    auto code = Network::SocketServiceError(json["error"].toInt());
    qDebug() << "[WS] Error (parsed)" << int(code);
    emit error(code);
}

const QString &WebSocketService::ip() const
{
    return m_ip;
}

quint16 WebSocketService::port() const
{
    return m_ws->localPort();
}

QString WebSocketService::protocolString() const
{
    return "WebSocket";
}

Network::Protocol WebSocketService::protocol() const
{
    return Network::Protocol::WebSocket;
}
