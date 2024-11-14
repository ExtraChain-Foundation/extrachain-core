#include "network/isocket_service.h"
#include "blockchain/actor_index.h"
#include <QJsonObject>

#include "encryption/encryption_tools.h"
#include "network/network_manager.h"

#ifndef EXTRACHAIN_CMAKE
    #include "preconfig.h"
#endif

SocketService::SocketService(ExtraChainNode *node, QObject *parent)
    : node(node)
    , QObject(parent) {
    priv.generate();
}

const QString &SocketService::identifier() const {
    return m_identifier;
}

QString SocketService::protocolString() const {
    return "Undefined";
}

Network::Protocol SocketService::protocol() const {
    return Network::Protocol::Undefined;
}

const QString &SocketService::ip() const {
    return m_ip;
}

const SocketService::SendType SocketService::sendType() const {
    return m_sendType;
}

int SocketService::bytesCompressed() const {
    return m_bytesCompressed;
}

int SocketService::bytesOutgoing() const {
    return m_bytesOutgoing;
}

int SocketService::bytesIncoming() const {
    return m_bytesIncoming;
}

bool SocketService::isConstant() const {
    return m_isConstant.load();
}

void SocketService::setConstant(bool isConstant) {
    m_isConstant = isConstant;
}

bool SocketService::checkFirstMessage(const QString &message, const bool canUseConnection) {
    auto json = QJsonDocument::fromJson(message.toLatin1());

    if (json.isEmpty()) {
        qDebug() << QString("[Socket] First message:%1").arg(message);
        closeSocket();
        eFatal("[Socket] Can't check first message");
        return false;
    }

    auto version                  = json["version"].toString();
    m_identifier                  = json["identifier"].toString();
    m_sendType                    = SendType(json["sendType"].toInt());
    ActorId    jsonFirstId        = ActorId(json["firstId"].toString().toStdString());
    ActorId    currentFirstId     = node->actorIndex()->firstId();
    bool       isFirstIdsContains = currentFirstId == jsonFirstId;
    bool       somethingEmpty     = jsonFirstId.is_zero() || currentFirstId.is_zero();
    QJsonArray connectionsArr     = json["connections"].toArray();

    qDebug() << QString("[Socket] First message:%1 | Current first:%2")
                    .arg(json.toJson())
                    .arg(currentFirstId.toQString());

    if (currentFirstId.is_zero() && !jsonFirstId.is_zero()) { // TODO: remove hack
        node->actorIndex()->setFirstId(jsonFirstId);
    }

    if (version != EXTRACHAIN_VERSION) {
        qDebug() << "[Socket] Close, because version incompatible" << EXTRACHAIN_VERSION;
        emit error(Network::SocketServiceError::IncompatibleVersion, version);
        closeSocket();
    }

    if (!(somethingEmpty || isFirstIdsContains)) {
        qDebug() << "[Socket] Close, because network incompatible";
        emit error(Network::SocketServiceError::IncompatibleNetwork, jsonFirstId.toQString());
        closeSocket();
        return false;
    }

    if (m_identifier == Network::currentIdentifier()) {
        emit error(Network::SocketServiceError::IncompatibleIdentifier, "");
        closeSocket();
        return false;
    }

    bool flag              = false;
    auto connectionsLocked = *node->network()->connections();
    std::for_each(connectionsLocked->begin(), connectionsLocked->end(), [&flag, this](SocketService *el) {
        flag = flag || (this != el && el->identifier() == m_identifier);
    });

    if (flag) {
        emit error(Network::SocketServiceError::DuplicateIdentifier, "");
        qDebug() << "[Socket] Duplicate identifier";
        closeSocket();
        return false;
    }

    if (canUseConnection) {
        qDebug() << "[Socket] Activated" << this << ip() << protocol();
        m_activated = true;
        emit activated();
        emit shareConnections(connectionsArr);
        return true;
    } else {
        qDebug() << "[Socket] Ignored as external node don't have enough slots." << this << protocol();
        m_activated    = false;
        m_needToDelete = true;
        return false;
    }
}

void SocketService::closeSocket() {
    m_activated = false;
}

QByteArray SocketService::generateFirstMessage() {
    QJsonObject json;
    json["firstId"]    = node->actorIndex()->firstId().toQString();
    json["version"]    = EXTRACHAIN_VERSION;
    json["identifier"] = QString(Network::currentIdentifier());
    json["sendType"]   = QString::number(int(m_sendType));

    QJsonArray connectionsArr;
    {
        auto connectionsLocked = *node->network()->connections();
        for (auto &it : *connectionsLocked)
            connectionsArr.append(it->ip());
    }
    json["connections"] = connectionsArr;

    QByteArray result = QJsonDocument(json).toJson(QJsonDocument::JsonFormat::Compact);
    return result;
}

QByteArray SocketService::prepareSendMessage(const QByteArray &message) {
    if (pub.empty())
        eFatal("Socket encrypt error");

    auto result = ByteArray(priv.encrypt(ByteArray(message).toBytes(), pub.publicKey())).toQByteArray();
    m_bytesOutgoing += result.length();
    return result;
}

QByteArray SocketService::prepareReceiveMessage(const QByteArray &message) {
    if (pub.empty())
        eFatal("Socket decrypt error");

    auto result = ByteArray(priv.decrypt(ByteArray(message).toBytes(), pub.publicKey())).toQByteArray();
    if (result.isEmpty())
        return "";
    m_bytesIncoming += message.length();
    return result;
}
