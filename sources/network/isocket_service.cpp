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

#include "extrachain_version.h"
#include "dfs/dfs_controller.h"
#include "chain/actor_index.h"
#include "network/network_manager.h"

#ifndef EXTRACHAIN_CMAKE
    #include "preconfig.h"
#endif

SocketService::SocketService(ExtraChainNode *node, QObject *parent)
    : node(node)
    , QObject(parent) {
    priv_.generate_random();
}

const QString &SocketService::identifier() const {
    return identifier_;
}

QString SocketService::protocol_string() const {
    return "Undefined";
}

Network::Protocol SocketService::protocol() const {
    return Network::Protocol::Undefined;
}

const QString &SocketService::ip() const {
    return ip_;
}

DfsMode SocketService::dfs_mode_socket() const {
    return dfs_mode_socket_;
}

int SocketService::bytes_compressed() const {
    return bytes_compressed_;
}

int SocketService::bytes_outgoing() const {
    return bytes_outgoing_;
}

int SocketService::bytes_incoming() const {
    return bytes_incoming_;
}

bool SocketService::is_constant() const {
    return is_constant_.load();
}

void SocketService::set_constant(bool isConstant) {
    is_constant_ = isConstant;
}

std::uint64_t SocketService::timestamp() const {
    return timestamp_;
}

bool SocketService::is_closed() {
    return closed_;
}

bool SocketService::check_first_message(const HandshakeMessage &handshake) {
    // eLog("[Socket] First message: {}", handshake);
    eLog("[Socket] First message: {} | IP: {} | network id: {}", direction_, ip_, node->actor_index()->network_id());

    // eLog("[Socket] First message: {} | Current network id: {} | IP: {}",
    //      handshake,
    //      node->actorIndex()->network_id(),
    //      ip_);

    identifier_      = QString::fromStdString(handshake.identifier);
    dfs_mode_socket_ = handshake.dfs_mode;

    peer_meta_ = PeerMeta {
        .version      = handshake.version,
        .node_version = handshake.node_version,
        .dag_version  = handshake.dag_version,
        .dfs_version  = std::nullopt,
        .dfs_mode     = handshake.dfs_mode,
    };

    if (peer_meta_.node_version.has_value()) {
        eLog("[Socket] Peer node_version: {}", *peer_meta_.node_version);
    }
    if (peer_meta_.dag_version.has_value()) {
        eLog("[Socket] Peer dag_version: {}", *peer_meta_.dag_version);
    } else {
        eLog("[Socket] Peer is legacy (no dag_version)");
    }

    // 0. Check mode
    if (handshake.socket_mode == SocketMode::Light) { // if full -> nothing change, because we can replace light
        mode_ = SocketMode::Light;
    }

    // 1. Checking the version
    if (auto version_result = Utils::compare_versions(extrachain_version, handshake.version);
        version_result != Utils::VersionCompareResult::Same) {
        auto error_type = (version_result == Utils::VersionCompareResult::Newer)
                              ? Network::SocketServiceError::VersionTooNew
                              : Network::SocketServiceError::VersionTooOld;

        // TODO: for user
        eInfo("Please, update client");

        eLog("[Socket] {} Closing: version {} incompatible with {}", direction_, handshake.version, extrachain_version);
        emit error(error_type,
                   QString::fromStdString(handshake.version),
                   ip_.toStdString(),
                   identifier_.toStdString(),
                   direction_);
        return false;
    }

    // 2. Network id check
    auto json_network_id_creation         = ActorId::create(handshake.network_id);
    if (!json_network_id_creation.has_value()) {
        // error
        return false;
    }

    ActorId json_network_id         = json_network_id_creation.value();
    ActorId our_network_id          = node->actor_index()->network_id();
    bool    is_network_ids_contains = our_network_id == json_network_id;
    bool    something_empty         = json_network_id.is_zero() || our_network_id.is_zero();

    if (our_network_id.is_zero() && !json_network_id.is_zero()) {
        node->actor_index()->set_network_id(json_network_id); // TODO: request block 0?
    }

    if (!(something_empty || is_network_ids_contains)) {
        eLog("[Socket] {} Closing: network id mismatch (local: {}, remote: {})", direction_, our_network_id, json_network_id);
        emit error(Network::SocketServiceError::IncompatibleNetwork,
                   QString::fromStdString(handshake.network_id),
                   ip_.toStdString(),
                   identifier_.toStdString(),
                   direction_);
        return false;
    }

    // 3. Identifier check
    if (handshake.identifier == node->node_identifier()) {
        emit error(Network::SocketServiceError::IncompatibleIdentifier,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString(),
                   direction_);
        return false;
    }

    // 4. Checking for duplicate connections
    bool duplicate = false;

    {
        auto connections_locked = *node->network()->connections();
        for (auto el : *connections_locked) {
            // pointers
            if (this == el) {
                continue;
            }

            if (el->identifier() != identifier_) {
                continue;
            }

            // if (el->is_active()) {
            // duplicate = true;
            // }

            // if (!el->is_active()) {
            el->closeSocket();
            break;
            // }
        }
    }

    if (duplicate) {
        emit error(Network::SocketServiceError::DuplicateIdentifier,
                   "",
                   ip_.toStdString(),
                   identifier_.toStdString(),
                   direction_);
        eLog("[Socket] {} Closing: duplicate identifier", direction_);
        return false;
    }

    if (is_disconnected_) {
        return false;
    }

    // 5. Check constant
    if (!is_constant() && handshake.is_constant) {
        is_constant_ = true;
    }

    // 6.
    if (node->network()->active_connections_count() >= Network::maxConnections) {
        emit error(Network::SocketServiceError::MaxConnections, "", ip_.toStdString(), identifier_.toStdString(), direction_);
        eLog("[Socket] {} Closing: maximum connections reached", direction_);
        return false;
    }

    // 7. Checking slots availability
    if (!handshake.is_available) {
        eLog("[Socket] {} Closing: peer unavailable", direction_);
        emit error(Network::SocketServiceError::PeerUnavailable, "", ip_.toStdString(), identifier_.toStdString(), direction_);
        emit shareConnections(handshake.connections);
        return false;
    }

    // 8. If all checks are passed - activate the connection
    eLog("[Socket] {} Activated: {} with IP: {}", direction_, fmt::ptr(this), ip());
    activated_ = true;
    Responder responder(node->network());
    responder.add_identifier(identifier_.toStdString());
    node->actor_index()->send_system_actor(responder);
    emit activated();
    emit shareConnections(handshake.connections);

    node->network()->set_public_ip(handshake.your_ip);

    return true;
}

void SocketService::closeSocket() {
    activated_ = false;
}

QByteArray SocketService::generate_first_message() {
    HandshakeMessage msg { .network_id   = node->actor_index()->network_id().to_string(),
                           .version      = extrachain_version,
                           .identifier   = node->node_identifier(),
                           .your_ip      = ip_.toStdString(),
                           .connections  = {},
                           .is_available = true,
                           .is_constant  = is_constant_.load(),
                           .socket_mode  = mode_,
                           .dfs_mode     = node->dfs()->mode(),
                           .dag_version  = CURRENT_DAG_VERSION,
                           .node_version = extrachain_node_version };

    {
        auto connections_locked = *node->network()->connections();
        for (auto &it : *connections_locked) {
            auto ip = it->ip().toStdString();
            if (ip.empty() || ip == ip_ || ip == "127.0.0.1") {
                continue;
            }
            if (!it->is_active()) {
                continue;
            }

            msg.connections.insert(SocketPair { .ip = ip, .identifier = it->identifier_.toStdString() });
        }
    }

    msg.is_available = node->network()->active_connections_count() < Network::maxConnections;

    auto handshake = Json::serialize(msg);
    return QByteArray::fromStdString(handshake);
}

QByteArray SocketService::prepareSendMessage(const QByteArray &message) {
    if (pub_.empty()) {
        return "";
    }

    auto encrypt_result = priv_.encrypt(ByteArray(message).toBytes(), pub_.public_key());
    if (!encrypt_result.has_value()) {
        return "";
    }
    auto result = ByteArray(encrypt_result.value()).toQByteArray();
    bytes_outgoing_ += result.length();
    return result;
}

QByteArray SocketService::prepareReceiveMessage(const QByteArray &message) {
    if (pub_.empty())
        eFatal("Socket decrypt error");

    auto decrypt_result = priv_.decrypt(ByteArray(message).toBytes(), pub_.public_key());
    if (!decrypt_result.has_value()) {
        return "";
    }
    auto result = ByteArray(decrypt_result.value()).toQByteArray();
    if (result.isEmpty())
        return "";
    bytes_incoming_ += message.length();
    return result;
}
