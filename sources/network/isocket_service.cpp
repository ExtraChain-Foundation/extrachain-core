#include "network/isocket_service.h"
#include "network/network_manager.h"

#ifndef EXTRACHAIN_CMAKE
#include "preconfig.h"
#endif

SocketService::SocketService(NetworkManager *networkManager, QObject *parent)
    : QObject(parent)
{
    m_networkManager = networkManager;
    priv.create(ActorType::Wallet);
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

    if (json.isEmpty())
    {
        qDebug() << "[Socket] First message:" << message;
        qFatal("[Socket] Can't check first message");
    }

    auto version = json["version"].toString();
    m_identifier = json["identifier"].toString();
    pub = json["key"].toString().toStdString();
    ActorId jsonFirstId = ActorId(json["firstId"].toString().toLatin1());
    ActorId currentFirstId = m_networkManager->actorIndex()->firstId();
    bool isFirstIdsContains = currentFirstId == jsonFirstId.toByteArray();
    bool somethingEmpty = jsonFirstId.isEmpty() || currentFirstId.isEmpty();

    qDebug() << "[Socket] First message:" << json << "| Current first:" << currentFirstId;

    if (version != EXTRACHAIN_VERSION)
    {
        qDebug() << "[Socket] Close, because version incompatible";
        emit error(Network::SocketServiceError::IncompatibleVersion, version);
        closeSocket();
    }

    if (!(somethingEmpty || isFirstIdsContains))
    {
        qDebug() << "[Socket] Close, because network incompatible";
        emit error(Network::SocketServiceError::IncompatibleNetwork, jsonFirstId.toString());
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
    auto &connections = m_networkManager->connections();
    std::for_each(connections.begin(), connections.end(), [&flag, this](SocketService *el) {
        flag = flag || (this != el && el->identifier() == m_identifier);
    });

    if (flag)
    {
        emit error(Network::SocketServiceError::DuplicateIdentifier, "");
        qDebug() << "[Socket] Duplicate identifier";
        closeSocket();
        return false;
    }

    qDebug() << "[Socket] Activated" << this << protocol();
    // m_activated = true;
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
    json["key"] = QString::fromStdString(priv.key()->getPubKey());
    QByteArray result = QJsonDocument(json).toJson(QJsonDocument::JsonFormat::Compact);
    return result;
}

QByteArray SocketService::prepareSendMessage(const QByteArray &message)
{
    if (pub.empty())
        qFatal("Socket encrypt error");

    auto result = priv.key()->encrypt(qCompress(message), pub);
    m_bytesOutgoing += result.length();
    m_bytesCompressed += message.length() - result.length();
    return result;
}

QByteArray SocketService::prepareReceiveMessage(const QByteArray &message)
{
    if (pub.empty())
        qFatal("Socket decrypt error");

    auto result = qUncompress(priv.key()->decrypt(message, pub));
    m_bytesIncoming += message.length();
    m_bytesCompressed += result.length() - message.length();
    return result;
}
