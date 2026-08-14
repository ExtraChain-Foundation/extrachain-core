/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "network/isocket_service.h"

#include "extrachain_version.h"
#include "utils/exc_logs.h"
#include "utils/serialization.h"
#include "utils/version.h"

SocketService::SocketService(PeerContext& context)
    : context_(context) {
    private_key_.generate_random();
}

const std::string& SocketService::identifier() const noexcept {
    return identifier_;
}

const std::string& SocketService::ip() const noexcept {
    return ip_;
}

DfsMode SocketService::dfs_mode_socket() const noexcept {
    return dfs_mode_socket_;
}

std::int64_t SocketService::bytes_compressed() const noexcept {
    return bytes_compressed_.load(std::memory_order_relaxed);
}

std::int64_t SocketService::bytes_outgoing() const noexcept {
    return bytes_outgoing_.load(std::memory_order_relaxed);
}

std::int64_t SocketService::bytes_incoming() const noexcept {
    return bytes_incoming_.load(std::memory_order_relaxed);
}

bool SocketService::is_constant() const noexcept {
    return constant_.load(std::memory_order_acquire);
}

SocketMode SocketService::mode() const noexcept {
    return mode_;
}

SocketDirection SocketService::direction() const noexcept {
    return direction_;
}

std::uint64_t SocketService::timestamp() const noexcept {
    return timestamp_;
}

const PeerMeta& SocketService::peer_meta() const noexcept {
    return peer_meta_;
}

bool SocketService::is_closed() const noexcept {
    return closed_.load(std::memory_order_acquire);
}

std::size_t SocketService::queue_size() const {
    std::scoped_lock lock(queue_mutex_);
    return high_queue_.size() + normal_queue_.size() + low_queue_.size();
}

void SocketService::set_constant(bool constant) noexcept {
    constant_.store(constant, std::memory_order_release);
}

void SocketService::set_direction(SocketDirection direction) noexcept {
    direction_ = direction;
}

std::int64_t SocketService::pending_bytes() const noexcept {
    return queued_bytes_.load(std::memory_order_relaxed);
}

bool SocketService::check_first_message(const HandshakeMessage& handshake) {
    eLog("[Socket] First message: {} | IP: {} | network id: {}", direction_, ip_, context_.local_network_id());

    identifier_      = handshake.identifier;
    dfs_mode_socket_ = handshake.dfs_mode;
    peer_meta_       = PeerMeta {
              .version      = handshake.version,
              .node_version = handshake.node_version,
              .dag_version  = handshake.dag_version,
              .dfs_version  = std::nullopt,
              .capabilities = handshake.capabilities.value_or(std::set<std::string> {}),
              .dfs_mode     = handshake.dfs_mode,
    };

    if (handshake.socket_mode == SocketMode::Light) {
        mode_ = SocketMode::Light;
    }

    if (const auto result = Utils::compare_versions(extrachain_version, handshake.version);
        result != Utils::VersionCompareResult::Same) {
        const auto error = result == Utils::VersionCompareResult::Newer
                               ? Network::SocketServiceError::VersionTooNew
                               : Network::SocketServiceError::VersionTooOld;
        if (on_error) {
            on_error(shared_from_this(), error, handshake.version, ip_, identifier_, direction_);
        }
        return false;
    }

    const auto remote_network = ActorId::create(handshake.network_id);
    if (!remote_network.has_value()) {
        return false;
    }

    const auto local_network = context_.local_network_id();
    if (local_network.is_zero() && !remote_network.value().is_zero()) {
        context_.adopt_network_id(remote_network.value());
    }
    if (!local_network.is_zero() && !remote_network.value().is_zero() && local_network != remote_network.value()) {
        if (on_error) {
            on_error(shared_from_this(),
                     Network::SocketServiceError::IncompatibleNetwork,
                     handshake.network_id,
                     ip_,
                     identifier_,
                     direction_);
        }
        return false;
    }

    if (handshake.identifier == context_.local_node_identifier()) {
        if (on_error) {
            on_error(shared_from_this(),
                     Network::SocketServiceError::IncompatibleIdentifier,
                     {},
                     ip_,
                     identifier_,
                     direction_);
        }
        return false;
    }

    if (context_.has_active_duplicate(identifier_, this)) {
        if (on_error) {
            on_error(shared_from_this(),
                     Network::SocketServiceError::DuplicateIdentifier,
                     {},
                     ip_,
                     identifier_,
                     direction_);
        }
        return false;
    }

    if (disconnected_.load(std::memory_order_acquire)) {
        return false;
    }
    if (!is_constant() && handshake.is_constant) {
        set_constant(true);
    }
    if (context_.active_peer_count() >= context_.peer_limit()) {
        if (on_error) {
            on_error(shared_from_this(),
                     Network::SocketServiceError::MaxConnections,
                     {},
                     ip_,
                     identifier_,
                     direction_);
        }
        return false;
    }
    if (!handshake.is_available) {
        if (on_error) {
            on_error(shared_from_this(),
                     Network::SocketServiceError::PeerUnavailable,
                     {},
                     ip_,
                     identifier_,
                     direction_);
        }
        if (on_share_connections) {
            on_share_connections(shared_from_this(), handshake.connections);
        }
        return false;
    }

    activated_.store(true, std::memory_order_release);
    context_.peer_authenticated(identifier_, handshake.your_ip);
    if (on_activated) {
        on_activated(shared_from_this());
    }
    if (on_share_connections) {
        on_share_connections(shared_from_this(), handshake.connections);
    }
    return true;
}

void SocketService::close_connection() {
    activated_.store(false, std::memory_order_release);
    closed_.store(true, std::memory_order_release);
    if (!disconnected_.exchange(true, std::memory_order_acq_rel) && on_disconnected) {
        on_disconnected(shared_from_this());
    }
}

bool SocketService::wait_closed(std::chrono::milliseconds) {
    return is_closed();
}

SocketService::Data SocketService::generate_first_message() {
    HandshakeMessage message {
        .network_id   = context_.local_network_id().to_string(),
        .version      = extrachain_version,
        .identifier   = context_.local_node_identifier(),
        .your_ip      = ip_,
        .connections  = {},
        .is_available = true,
        .is_constant  = is_constant(),
        .socket_mode  = mode_,
        .dfs_mode     = context_.local_dfs_mode(),
        .dag_version  = CURRENT_DAG_VERSION,
        .node_version = extrachain_node_version,
        .capabilities = std::set<std::string> { std::string(DAG_TX_BATCH_CAPABILITY),
                                                std::string(TOKEN_MIGRATION_CAPABILITY) },
    };

    message.connections = context_.shareable_peers(ip_);

    message.is_available  = context_.active_peer_count() < context_.peer_limit();
    const auto serialized = Json::serialize(message);
    return { serialized.begin(), serialized.end() };
}

SocketService::Data SocketService::prepare_send_message(std::span<const std::uint8_t> message) {
    return prepare_send_message(Data(message.begin(), message.end()));
}

SocketService::Data SocketService::prepare_send_message(const Data& message) {
    if (public_key_.empty()) {
        return {};
    }
    const auto encrypted = private_key_.encrypt(message, public_key_.public_key());
    if (!encrypted.has_value()) {
        return {};
    }
    bytes_outgoing_.fetch_add(static_cast<std::int64_t>(encrypted.value().size()), std::memory_order_relaxed);
    return std::move(encrypted.value());
}

SocketService::Data SocketService::prepare_receive_message(std::span<const std::uint8_t> message) {
    return prepare_receive_message(Data(message.begin(), message.end()));
}

SocketService::Data SocketService::prepare_receive_message(const Data& message) {
    if (public_key_.empty()) {
        return {};
    }
    const auto decrypted = private_key_.decrypt(message, public_key_.public_key());
    if (!decrypted.has_value() || decrypted.value().empty()) {
        return {};
    }
    bytes_incoming_.fetch_add(static_cast<std::int64_t>(message.size()), std::memory_order_relaxed);
    return std::move(decrypted.value());
}
