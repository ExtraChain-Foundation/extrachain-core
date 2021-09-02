#include "network/isocket_service.h"
#include "network/network_manager.h"

SocketService::SocketService(NetworkManager *networkManager, QObject *parent)
    : QObject(parent)
{
    m_networkManager = networkManager;
}

SocketService::SocketService(const SocketService &socket)
{
    m_identifier = socket.m_identifier;
    m_ip = socket.m_ip;
    m_activated = socket.m_activated;
    m_bytesIncoming = socket.m_bytesIncoming;
    m_bytesOutgoing = socket.m_bytesOutgoing;
    m_bytesCompressed = socket.m_bytesCompressed;
}

const QString &SocketService::identifier() const
{
    return m_identifier;
}

QString SocketService::protocolString() const
{
    return "Undefined";
}

Network::Protocol SocketService::protocol() const
{
    return Network::Protocol::Undefined;
}

const QString &SocketService::ip() const
{
    return m_ip;
}

int SocketService::bytesCompressed() const
{
    return m_bytesCompressed;
}

int SocketService::bytesOutgoing() const
{
    return m_bytesOutgoing;
}

int SocketService::bytesIncoming() const
{
    return m_bytesIncoming;
}

bool SocketService::checkFirstMessage(const QString &message)
{
    auto json = QJsonDocument::fromJson(message.toLatin1());
    auto version = json["version"].toString();
    m_identifier = json["identifier"].toString();
    ActorId jsonFirstId = ActorId(json["firstId"].toString().toLatin1());
    ActorId currentFirstId = m_networkManager->actorIndex()->firstId();
    bool isFirstIdsContains = currentFirstId == jsonFirstId.toByteArray();
    bool somethingEmpty = jsonFirstId.isEmpty() || currentFirstId.isEmpty();

    qDebug() << "[WS]" << currentFirstId << jsonFirstId << currentFirstId.isEmpty() << jsonFirstId.isEmpty()
             << isFirstIdsContains << somethingEmpty << (version != EXTRACHAIN_VERSION);

    if (!(somethingEmpty || isFirstIdsContains) || version != EXTRACHAIN_VERSION)
    {
        qDebug() << "[WS] Close, because network or version unsuitable";
        version != EXTRACHAIN_VERSION
            ? emit error(Network::SocketServiceError::IncompatibleVersion, version)
            : emit error(Network::SocketServiceError::IncompatibleNetwork, jsonFirstId.toString());
        closeSocket();
        return false;
    }

    if (m_identifier == Network::currentIdentifier())
    {
        emit error(Network::SocketServiceError::IncompatibleIdentifier, "");
        closeSocket();
        return false;
    }

    bool flag = false;
    auto &wsConnections = m_networkManager->wsConnections();
    std::for_each(wsConnections.begin(), wsConnections.end(), [&flag, this](WebSocketService *el) {
        flag = flag || (this != el && el->identifier() == m_identifier);
    });

    if (flag)
    {
        emit error(Network::SocketServiceError::DuplicateIdentifier, "");
        qDebug() << "[WS] Duplicate identifier";
        closeSocket();
        return false;
    }

    qDebug() << "[Socket] Activated" << this << protocol();
    return true;
}

void SocketService::closeSocket()
{
    m_activated = false;
}

QByteArray SocketService::generateFirstMessage()
{
    QJsonObject json;
    json["firstId"] = m_networkManager->actorIndex()->firstId().toString();
    json["version"] = EXTRACHAIN_VERSION;
    json["identifier"] = QString(Network::currentIdentifier());
    return QJsonDocument(json).toJson(QJsonDocument::JsonFormat::Compact);
}
