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

#include "chain/actor_index.h"
#include "dfs/dfs_controller.h"
#include "extrachain_version.h"
#include "network/network_manager.h"

#ifndef EXTRACHAIN_CMAKE
    #include "preconfig.h"
#endif

SocketService::SocketService(ExtraChainNode *node)
    : node_(node) {
    priv_.generate_random();
}

const std::string &SocketService::identifier() const {
    return identifier_;
}

const std::string &SocketService::ip() const {
    return ip_;
}

DfsMode SocketService::dfs_mode_socket() const {
    return dfs_mode_socket_;
}

int64_t SocketService::bytes_compressed() const {
    return bytes_compressed_;
}

int64_t SocketService::bytes_outgoing() const {
    return bytes_outgoing_;
}

int64_t SocketService::bytes_incoming() const {
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

long SocketService::queue_size() {
    std::lock_guard<std::mutex> locker(queue_mutex_);
    return static_cast<long>(high_queue_.size() + normal_queue_.size() + low_queue_.size());
}

bool SocketService::check_first_message(const HandshakeMessage &handshake) {
    eLog("[Socket] First message: {} | IP: {} | network id: {}", direction_, ip_, node_->actor_index()->network_id());

    identifier_      = handshake.identifier;
    dfs_mode_socket_ = handshake.dfs_mode;

    if (handshake.socket_mode == SocketMode::Light) {
        mode_ = SocketMode::Light;
    }

    if (auto version_result = Utils::compare_versions(extrachain_version, handshake.version);
        version_result != Utils::VersionCompareResult::Same) {
        auto error_type = (version_result == Utils::VersionCompareResult::Newer)
                              ? Network::SocketServiceError::VersionTooNew
                              : Network::SocketServiceError::VersionTooOld;

        eInfo("Please, update client");
        eLog("[Socket] Closing: version {} incompatible with {} | {}", handshake.version, extrachain_version, direction_);
        if (on_error) {
            on_error(shared_from_this(), error_type, handshake.version, identifier_);
        }
        return false;
    }

    auto json_network_id_creation = ActorId::create(handshake.network_id);
    if (!json_network_id_creation.has_value()) {
        return false;
    }

    ActorId json_network_id         = json_network_id_creation.value();
    ActorId our_network_id          = node_->actor_index()->network_id();
    bool    is_network_ids_contains = our_network_id == json_network_id;
    bool    something_empty         = json_network_id.is_zero() || our_network_id.is_zero();

    if (our_network_id.is_zero() && !json_network_id.is_zero()) {
        node_->actor_index()->set_network_id(json_network_id);
    }

    if (!(something_empty || is_network_ids_contains)) {
        eLog("[Socket] Closing: network id mismatch (local: {}, remote: {}) | {}", our_network_id, json_network_id, direction_);
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncompatibleNetwork, handshake.network_id, identifier_);
        }
        return false;
    }

    if (handshake.identifier == node_->node_identifier()) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::IncompatibleIdentifier, "", identifier_);
        }
        return false;
    }

    {
        auto connections_locked = *node_->network()->connections();
        for (const auto &el : *connections_locked) {
            if (el.get() == this) {
                continue;
            }

            if (el->identifier() != identifier_) {
                continue;
            }

            el->close_connection();
            break;
        }
    }

    if (is_disconnected_) {
        return false;
    }

    if (!is_constant() && handshake.is_constant) {
        is_constant_ = true;
    }

    if (node_->network()->active_connections_count() >= Network::maxConnections) {
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::MaxConnections, "", identifier_);
        }
        eLog("[Socket] Closing: maximum connections reached | {}", direction_);
        return false;
    }

    if (!handshake.is_available) {
        eLog("[Socket] Closing: peer unavailable | {}", direction_);
        if (on_error) {
            on_error(shared_from_this(), Network::SocketServiceError::PeerUnavailable, "", identifier_);
        }
        if (on_share_connections) {
            on_share_connections(shared_from_this(), handshake.connections);
        }
        return false;
    }

    eLog("[Socket] {} Activated: {} with IP: {}", direction_, fmt::ptr(this), ip());
    activated_ = true;

    Responder responder(node_->network());
    responder.add_identifier(identifier_);
    node_->actor_index()->send_system_actor(responder);

    if (on_activated) {
        on_activated(shared_from_this());
    }
    if (on_share_connections) {
        on_share_connections(shared_from_this(), handshake.connections);
    }

    node_->network()->set_public_ip(handshake.your_ip);
    return true;
}

void SocketService::close_connection() {
    activated_ = false;

    if (!is_disconnected_) {
        is_disconnected_ = true;
        if (on_disconnected) {
            on_disconnected(shared_from_this());
        }
    }
}

std::vector<uint8_t> SocketService::generate_first_message() {
    HandshakeMessage msg { .network_id   = node_->actor_index()->network_id().to_string(),
                           .version      = extrachain_version,
                           .identifier   = node_->node_identifier(),
                           .your_ip      = ip_,
                           .connections  = {},
                           .is_available = true,
                           .is_constant  = is_constant_.load(),
                           .socket_mode  = mode_,
                           .dfs_mode     = node_->dfs()->mode() };

    {
        auto connections_locked = *node_->network()->connections();
        for (const auto &it : *connections_locked) {
            const auto &conn_ip = it->ip();
            if (conn_ip.empty() || conn_ip == ip_ || conn_ip == "127.0.0.1") {
                continue;
            }
            if (!it->is_active()) {
                continue;
            }

            msg.connections.insert(SocketPair { .ip = conn_ip, .identifier = it->identifier() });
        }
    }

    msg.is_available = node_->network()->active_connections_count() < Network::maxConnections;

    auto handshake = Json::serialize(msg);
    return std::vector<uint8_t>(handshake.begin(), handshake.end());
}

std::vector<uint8_t> SocketService::prepare_send_message(const std::vector<uint8_t> &message) {
    if (pub_.empty()) {
        return {};
    }

    auto encrypt_result = priv_.encrypt(message, pub_.public_key());
    if (!encrypt_result.has_value()) {
        return {};
    }

    bytes_outgoing_ += static_cast<int64_t>(encrypt_result->size());
    return encrypt_result.value();
}

std::vector<uint8_t> SocketService::prepare_receive_message(const std::vector<uint8_t> &message) {
    if (pub_.empty()) {
        eCritical("[Socket] Decrypt error: public key not set");
        return {};
    }

    auto decrypt_result = priv_.decrypt(message, pub_.public_key());
    if (!decrypt_result.has_value() || decrypt_result->empty()) {
        return {};
    }

    bytes_incoming_ += static_cast<int64_t>(message.size());
    return decrypt_result.value();
}
