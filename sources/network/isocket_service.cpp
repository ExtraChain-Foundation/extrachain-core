/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

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

bool SocketService::isVPN() const {
    return m_isConstant.load();
}

void SocketService::setVPN(bool isVPN) {
    m_isVPN = isVPN;
}

bool SocketService::checkFirstMessage(const QString &message, const bool canUseConnection) {
    auto json = QJsonDocument::fromJson(message.toLatin1());

    if (json.isEmpty()) {
        eLog("[Socket] First message: '{}'", message);
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

    eLog("[Socket] First message: {} | Current first: {}", json.toJson(QJsonDocument::Compact), currentFirstId);

    if (currentFirstId.is_zero() && !jsonFirstId.is_zero()) { // TODO: remove hack
        node->actorIndex()->setFirstId(jsonFirstId);
    }

    if (version != EXTRACHAIN_VERSION) {
        eLog("[Socket] Close, because version incompatible {}", EXTRACHAIN_VERSION);
        emit error(Network::SocketServiceError::IncompatibleVersion, version);
        closeSocket();
    }

    if (!(somethingEmpty || isFirstIdsContains)) {
        eLog("[Socket] Close, because network incompatible");
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
        eLog("[Socket] Duplicate identifier");
        closeSocket();
        return false;
    }

    if (canUseConnection) {
        eLog("[Socket] Activated {} {} {}", fmt::ptr(this), ip(), protocol());
        m_activated = true;
        emit activated();
        emit shareConnections(connectionsArr);
        return true;
    } else {
        eLog("[Socket] Ignored as external node don't have enough slots. {} {}", fmt::ptr(this), protocol());
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

    auto result = ByteArray(priv.encrypt(ByteArray(message).toBytes(), pub.public_key())).toQByteArray();
    m_bytesOutgoing += result.length();
    return result;
}

QByteArray SocketService::prepareReceiveMessage(const QByteArray &message) {
    if (pub.empty())
        eFatal("Socket decrypt error");

    auto result = ByteArray(priv.decrypt(ByteArray(message).toBytes(), pub.public_key())).toQByteArray();
    if (result.isEmpty())
        return "";
    m_bytesIncoming += message.length();
    return result;
}
