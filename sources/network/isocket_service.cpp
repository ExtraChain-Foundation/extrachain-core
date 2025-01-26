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

bool SocketService::checkFirstMessage(const HandshakeMessage &handshake) {
    eLog("[Socket] First message: {} | Current first: {}", handshake, node->actorIndex()->firstId());
    m_identifier = QString::fromStdString(handshake.identifier);
    m_sendType   = handshake.send_type;

    // 1. Checking the version
    if (handshake.version != EXTRACHAIN_VERSION) {
        eLog("[Socket] Close, because version incompatible {}", EXTRACHAIN_VERSION);
        emit error(Network::SocketServiceError::IncompatibleVersion,
                   QString::fromStdString(handshake.version),
                   m_ip.toStdString(),
                   m_identifier.toStdString());
        closeSocket();
        return false;
    }

    // 2. First id/network checks
    ActorId json_first_id         = ActorId(handshake.first_id);
    ActorId current_first_id      = node->actorIndex()->firstId();
    bool    is_first_ids_contains = current_first_id == json_first_id;
    bool    something_empty       = json_first_id.is_zero() || current_first_id.is_zero();

    if (current_first_id.is_zero() && !json_first_id.is_zero()) {
        node->actorIndex()->setFirstId(json_first_id); // TODO: request block 0?
    }

    if (!(something_empty || is_first_ids_contains)) {
        eLog("[Socket] Close, because network incompatible");
        emit error(Network::SocketServiceError::IncompatibleNetwork,
                   QString::fromStdString(handshake.first_id),
                   m_ip.toStdString(),
                   m_identifier.toStdString());
        closeSocket();
        return false;
    }

    // 3. Identifier check
    if (handshake.identifier == Network::currentIdentifier()) {
        emit error(Network::SocketServiceError::IncompatibleIdentifier,
                   "",
                   m_ip.toStdString(),
                   m_identifier.toStdString());
        closeSocket();
        return false;
    }

    // 4. Checking for duplicate connections
    bool duplicate = false;
    {
        auto connections_locked = *node->network()->connections();
        std::for_each(connections_locked->begin(),
                      connections_locked->end(),
                      [&duplicate, this](SocketService *el) {
                          duplicate = duplicate || (this != el && el->identifier() == m_identifier);
                      });
    }

    if (duplicate) {
        emit error(Network::SocketServiceError::DuplicateIdentifier,
                   "",
                   m_ip.toStdString(),
                   m_identifier.toStdString());
        eLog("[Socket] Duplicate identifier");
        closeSocket();
        return false;
    }

    if (is_disconnected) {
        return false;
    }

    // 5. Check constant
    if (!isConstant() && handshake.is_constant) {
        m_isConstant = true;
    }

    // 6.
    {
        auto connections_locked = *node->network()->connections();
        if (connections_locked->size() >= Network::maxConnections) {
            emit error(Network::SocketServiceError::MaxConnections,
                       "",
                       m_ip.toStdString(),
                       m_identifier.toStdString());
            eLog("[Socket] Max connections");
            closeSocket();
            return false;
        }
    }

    // 7. Checking slots availability
    if (!handshake.is_available) {
        eLog("[Socket] Peer not available");
        closeSocket();
        emit shareConnections(handshake.connections);
        return false;
    }

    // 8. If all checks are passed - activate the connection
    eLog("[Socket] Activated {} {} {}", fmt::ptr(this), ip(), protocol());
    m_activated = true;
    emit activated();
    emit shareConnections(handshake.connections);

    return true;
}

void SocketService::closeSocket() {
    m_activated = false;
}

QByteArray SocketService::generateFirstMessage() {
    HandshakeMessage msg { .first_id     = node->actorIndex()->firstId().to_string(),
                           .version      = EXTRACHAIN_VERSION,
                           .identifier   = Network::currentIdentifier().toStdString(),
                           .send_type    = m_sendType,
                           .connections  = {},
                           .is_available = true,
                           .is_constant  = m_isConstant.load() };

    {
        auto connections_locked = *node->network()->connections();
        for (auto &it : *connections_locked) {
            auto ip = it->ip().toStdString();
            if (ip.empty()) {
                continue;
            }

            msg.connections.insert(ip);
        }
        msg.is_available = connections_locked->size() < Network::maxConnections;
    }

    auto handshake = Json::serialize(msg);
    return QByteArray::fromStdString(handshake);
}

QByteArray SocketService::prepareSendMessage(const QByteArray &message) {
    if (pub.empty())
        eFatal("Socket encrypt error");

    auto encrypt_result = priv.encrypt(ByteArray(message).toBytes(), pub.public_key());
    if (!encrypt_result.has_value()) {
        return "";
    }
    auto result = ByteArray(encrypt_result.value()).toQByteArray();
    m_bytesOutgoing += result.length();
    return result;
}

QByteArray SocketService::prepareReceiveMessage(const QByteArray &message) {
    if (pub.empty())
        eFatal("Socket decrypt error");

    auto decrypt_result = priv.decrypt(ByteArray(message).toBytes(), pub.public_key());
    if (!decrypt_result.has_value()) {
        return "";
    }
    auto result = ByteArray(decrypt_result.value()).toQByteArray();
    if (result.isEmpty())
        return "";
    m_bytesIncoming += message.length();
    return result;
}
