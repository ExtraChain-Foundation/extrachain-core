#include "network/websocket_service.h"

#include "dfs/managers/headers/dfs_networkmanager.h"

#ifndef EXTRACHAIN_CMAKE
#include "preconfig.h"
#endif

WebSocketService::WebSocketService(QWebSocket *ws, NetworkManager *newNetworkManager,
                                   ActorIndex *newActorIndex, QObject *parent)
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
    activated = false;
    m_ws->close();
    emit disconnected();
}

int WebSocketService::bytesCompressed() const
{
    return m_bytesCompressed;
}

int WebSocketService::bytesOutgoing() const
{
    return m_bytesOutgoing;
}

int WebSocketService::bytesIncoming() const
{
    return m_bytesIncoming;
}

bool WebSocketService::operator==(const WebSocketService &service) const
{
    return m_ws == service.m_ws;
}

void WebSocketService::onTextMessage(const QString &message) // for first message and errors
{
    qDebug() << "[WS] Text:" << message;

    // if (message.left(8) == "{\"error\"") { }

    if (!activated)
    {
        auto json = QJsonDocument::fromJson(message.toLatin1());
        auto version = json["version"].toString();
        m_identifier = json["identifier"].toString();
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
            version != EXTRACHAIN_VERSION
                ? error(Network::SocketServiceError::IncompatibleVersion, version)
                : error(Network::SocketServiceError::IncompatibleNetwork, jsonFirstId.toString());
            closeSocket();
            return;
        }

        if (m_identifier == Network::currentIdentifier())
        {
            error(Network::SocketServiceError::IncompatibleIdentifier, "");
            closeSocket();
            return;
        }

        bool flag = false;
        auto &wsConnections = networkManager->getWsConnections();
        std::for_each(wsConnections.begin(), wsConnections.end(), [&flag, this](WebSocketService *el) {
            flag = flag || (this != el && el->identifier() == m_identifier);
        });
        qDebug() << "[WS] Flag:" << flag;
        if (flag)
        {
            error(Network::SocketServiceError::DuplicateIdentifier, "");
            qDebug() << "[WS] Duplicate identifier";
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
    m_bytesCompressed += mess.length() - message.length();
    m_bytesIncoming += message.length();
    networkManager->MessageReceived(mess, pair);
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
{
    qDebug() << "[WS] Socket error:" << error;

    if (m_ws->state() != QAbstractSocket::ConnectedState)
        closeSocket();
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
    qDebug() << "[WS] Send first message" << port();
    QJsonObject json;
    json["firstId"] = actorIndex->firstId().toString();
    json["version"] = EXTRACHAIN_VERSION;
    json["identifier"] = QString(Network::currentIdentifier());
    QByteArray jsonBytes = QJsonDocument(json).toJson(QJsonDocument::JsonFormat::Compact);
    m_ws->sendTextMessage(jsonBytes);
}

const QString &WebSocketService::ip() const
{
    return m_ip;
}

quint16 WebSocketService::port() const
{
    if (m_ws->peerPort() != networkManager->wsPort)
        return m_ws->peerPort();
    else
        return m_ws->localPort();
}

quint16 WebSocketService::serverPort() const
{
    if (m_ws->peerPort() == networkManager->wsPort)
        return m_ws->peerPort();
    else
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
