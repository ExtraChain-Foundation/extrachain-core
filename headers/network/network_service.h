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

#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include "core/extrachain_node.h"
#include "dfs/dfs_utils.h"
#include "managers/account_controller.h"
#include "network/isocket_service.h"
#include "network/message_body.h"
#include "network/network_runtime.h"
#include "network/responder.h"
#include "network/network_status.h"
#include "network/traffic_meter.h"
#include "network/wire_format.h"
#include "runtime/deadline_task.h"
#include "runtime/event.h"
#include "utils/exc_utils.h"
#include "utils/safeptr.h"

class SocketService;
class WebSocketService;
using CalculateTraffic = ExtraChain::Core::TrafficMeter;

struct NetworkReconnect {
    std::string       ip;
    std::uint16_t     port;
    Network::Protocol protocol;

    auto operator==(const NetworkReconnect& reconnect) const {
        return ip == reconnect.ip && port == reconnect.port && protocol == reconnect.protocol;
    }

    bool operator<(const NetworkReconnect& other) const {
        if (ip < other.ip)
            return true;
        if (ip == other.ip) {
            if (port < other.port)
                return true;
            if (port == other.port)
                return protocol < other.protocol;
        }

        return false;
    }

    static NetworkReconnect fromWsConnection(const DfsP::WSConnection& wsConnection) {
        return NetworkReconnect { .ip       = wsConnection.address,
                                  .port     = static_cast<std::uint16_t>(wsConnection.port),
                                  .protocol = Network::Protocol::WebSocket };
    }

    void print() const {
        eLog("[NetworkReconnect] ip: {}, port: {}", ip, port);
    }
};

inline constexpr char NetworkCacheFile[] = "tmp/network.cache";

/**
 * @brief The NetworkManager class
 * Creates Discovery, Server and Sockets services
 */
class EXTRACHAIN_EXPORT NetworkService : public PeerContext, public ResponseSender {

public:
    using SocketActivatedEvent = ExtraChain::Core::Event<const std::string&, const std::string&>;
    using ConnectionStateEvent = ExtraChain::Core::Event<bool, int>;
    using ConnectionErrorEvent = ExtraChain::Core::
        Event<Network::SocketServiceError, const std::string&, const std::string&, const std::string&>;
    using CustomMessageEvent = ExtraChain::Core::Event<const NetworkPackageStorage&, const CustomMessage&>;

private:
    using CacheTime = std::chrono::steady_clock::time_point;

    enum class PeerSelection {
        All,
        LegacyDag
    };

    struct LiveDagQueueEntry {
        Transaction transaction;
        std::size_t serialized_size = 0;
    };

    struct LiveDagPeerQueue {
        std::deque<LiveDagQueueEntry> entries;
        std::size_t                   bytes = 0;
    };

    bool                                                       active_ = false;
    std::set<std::string>                                      failed_ips_;
    std::atomic_bool                                           first_node_self_detected_ { false };
    std::unordered_map<std::string, std::pair<int, CacheTime>> msg_hash_list_;

    ExtraChain::Core::ExtraChainNode*                         node;
    std::string                                               local_ip_;
    ExtraChain::Core::NetworkRuntime*                         network_runtime_ = nullptr;
    boost::asio::any_io_executor                              serial_executor_;
    std::atomic_bool                                          stopping_ { false };
    SafePtr<std::set<SocketService::Ptr>>                     connections_;
    SafePtr<std::map<NetworkReconnect, std::string>>          reconnections_to_identifier_;
    ExtraChain::Core::NetworkStatus                           network_status_;
    ExtraChain::Core::NetworkStatus::ChangedEvent::Connection network_status_connection_;
    ExtraChain::Core::Event<RuntimeActivity>::Connection      runtime_activity_connection_;

    struct ReconnEntry {
        std::uint64_t attempts        = 0;
        std::int64_t  next_attempt_ms = 0;
    };
    std::map<std::string, ReconnEntry> reconn_;

    SafePtr<std::map<std::string, std::pair<std::string, CacheTime>>>           messages_;
    std::shared_ptr<ExtraChain::Core::DeadlineTask>                             reconnect_timer_;
    std::atomic_bool                                                            offline_ { false };
    std::atomic_bool                                                            first_node_probe_active_ { false };
    std::shared_ptr<ExtraChain::Core::DeadlineTask>                             clear_network_caches_timer_;
    std::shared_ptr<ExtraChain::Core::DeadlineTask>                             live_dag_batch_timer_;
    CalculateTraffic*                                                           calculate_traffic_;
    SafePtr<std::unordered_map<std::string, std::pair<std::string, CacheTime>>> forwarded_messages_;
    std::unordered_map<std::string, LiveDagPeerQueue>                           live_dag_peer_queues_;
    std::mutex                                                                  recent_dag_hashes_mutex_;
    std::unordered_set<std::string>                                             recent_dag_hashes_;
    std::deque<std::string>                                                     recent_dag_hash_order_;
    SocketActivatedEvent                                                        socket_activated_event_;
    ExtraChain::Core::Event<>                                                   socket_ready_event_;
    ConnectionStateEvent                                                        connection_state_event_;
    ConnectionErrorEvent                                                        connection_error_event_;
    CustomMessageEvent                                                          custom_message_event_;

    std::string              public_ip_;
    std::vector<std::string> first_nodes_ =
#ifndef NDEBUG
        {
            "57.128.191.73", // test node 1
            "57.128.191.74", // test node 2
            "57.128.200.221" // test node 3
        };
#else
        {
            "51.68.181.52", // release node 1
            "149.33.19.250" // release node 2
        };
#endif
    std::string first_node_;

public:
    explicit NetworkService(ExtraChain::Core::ExtraChainNode* node,
                            ExtraChain::Core::NetworkRuntime& runtime,
                            std::uint16_t                     port);
    virtual ~NetworkService();
    void                                prepare_shutdown();
    void                                local_inizialization();
    void                                probe_first_node_candidate(std::size_t index);
    std::pair<std::string, std::string> public_ip_and_country(std::string ip = {}, bool alt = false);

    bool                                           remove_one_connection();
    [[nodiscard]] SocketActivatedEvent&            socket_activated_event() noexcept;
    [[nodiscard]] ExtraChain::Core::Event<>&       socket_ready_event() noexcept;
    [[nodiscard]] ConnectionStateEvent&            connection_state_event() noexcept;
    [[nodiscard]] ConnectionErrorEvent&            connection_error_event() noexcept;
    [[nodiscard]] CustomMessageEvent&              custom_message_event() noexcept;
    [[nodiscard]] ExtraChain::Core::NetworkStatus& network_status() noexcept;

private:
    std::uint16_t ws_port_ = 17593;

    void connectWsService(const std::shared_ptr<WebSocketService>& service, bool requestListNodes = false);
    boost::asio::awaitable<void> connect_websocket(std::string   ip,
                                                   std::uint16_t port,
                                                   bool          request_list_nodes,
                                                   bool          is_constant,
                                                   bool          is_light);

    void send_message_connections(const std::string& serialized_message,
                                  const std::string& serialized_message_legacy,
                                  const MessageBody& non_serialized_message,
                                  SendMode           send_mode,
                                  const std::string& receiver_identifier,
                                  MessageType        message_type   = MessageType::Unknown,
                                  MessageStatus      status_info    = MessageStatus::NoStatus,
                                  PeerSelection      peer_selection = PeerSelection::All);

    void queue_live_dag_transaction(const Transaction&                     transaction,
                                    const std::string&                     source_identifier,
                                    const std::unordered_set<std::string>& ignored_identifiers);
    void flush_live_dag_batches();
    void send_live_dag_batch(const std::string& peer_identifier, DagTransactionBatch batch);
    void send_live_dag_transaction_to_legacy(const Transaction& transaction);
    bool remember_live_dag_hash(const std::string& hash);
    void forget_live_dag_hash(const std::string& hash);

    void clear_network_caches();
    void schedule_cache_cleanup();
    void schedule_reconnection(int delay_ms);
    void dispatch_serial(std::function<void()> handler);
    void queue_live_dag_transaction_serial(Transaction                     transaction,
                                           std::string                     source_identifier,
                                           std::unordered_set<std::string> ignored_identifiers);

    void add_all_services_identifiers_to_message(MessageBody& msg);
    void refresh_public_ip_and_country(std::string ip = {});

public:
    bool                                  is_first_node(const std::string& identifier); // detect for safety
    SafePtr<std::set<SocketService::Ptr>> connections() const;

    // Look up an active connection's peer_meta by its node identifier (the
    // string carried by Responder). Returns nullopt when the peer is not
    // currently connected. Response handlers must reject data when metadata is
    // absent; request handlers may use the legacy format for old peers.
    std::optional<PeerMeta> peer_meta_for(const std::string& identifier) const;
    bool                    server_status(Network::Protocol protocol = Network::Protocol::WebSocket) const;
    void                    connect_network();
    void                    request_connection(std::string       ip,
                                               Network::Protocol protocol    = Network::Protocol::WebSocket,
                                               bool              request     = false,
                                               bool              is_constant = false,
                                               bool              is_light    = false);
    void                    request_endpoint(std::string   ip,
                                             std::uint16_t port,
                                             bool          request_list_nodes = false,
                                             bool          is_constant        = false,
                                             bool          is_light           = false);
    void                    disconnect_peer(std::string identifier);
    bool                    is_own_address(const std::string& ip) const;

protected:
    /**
     * @brief NetworkManager::check_message_count
     * @param msg
     * @return
     */
    bool check_message_count(const std::string& msg);

public:
    std::size_t msg_hash_list_size() const {
        return msg_hash_list_.size();
    }
    std::size_t messages_size() {
        return messages_->size();
    }
    std::size_t forwarded_messages_size() {
        return forwarded_messages_->size();
    }
    std::size_t connections_size() {
        return connections_->size();
    }

protected:
    virtual void check_connections_status();

public:
    void start_network();
    // Permanent offline for a revoked client: closes every socket, stops the
    // reconnect timer and refuses any new connections until process exit.
    void go_offline();
    void process();
    void reconnection();

private:
    void check_port(std::string ip, Network::Protocol protocol, bool request, bool is_constant, bool is_light);
    void connect_node(std::string ip, Network::Protocol protocol, bool request, bool is_constant, bool is_light);
    void connect_to_websocket(std::string   ip,
                              std::uint16_t port,
                              bool          request_list_nodes,
                              bool          is_constant,
                              bool          is_light);
    void remove_socket_connection(SocketService::Ptr connection);
    void socket_error(Network::SocketServiceError error,
                      std::string                 error_data,
                      std::string                 ip,
                      std::string                 identifier,
                      SocketDirection             direction);

public:
    [[nodiscard]] const std::string& local_ip_value() const noexcept;

    void        initialize_first_node();
    std::string first_node();
    bool        save_first_node(const std::string_view first_node);

    void send_broadcast_message_further(const NetworkPackageStorage& package_data, bool legacy_dag_only = false);

    void                     save_to_cache(const std::string& serialized_message,
                                           SendMode           send_mode,
                                           const std::string& receiver_identifier);
    void                     send_from_cache();
    bool                     is_connection_exists(const std::string& identifier);
    bool                     is_active_connection_exists();
    int                      active_connections_count();
    int                      max_connections() const;
    std::vector<std::string> active_connection_identifiers() const;
    std::vector<std::string> active_full_peer_identifiers() const;
    std::vector<std::string> active_full_peers_with_capability(std::string_view capability) const;
    std::int64_t             connection_pending_bytes(const std::string& identifier) const;

    void message_received(const std::string& message, const std::string& ip, const std::string& identifier);

    bool        send_message_checker(MessageType      type,
                                     SendMode         send_mode,
                                     MessageStatus    status,
                                     const Responder& responder);
    std::string send_message_send(const std::string& data_serialized,
                                  const std::string& data_serialized_legacy,
                                  MessageType        type,
                                  SendMode           send_mode,
                                  MessageStatus      status,
                                  const Responder&   responder);
    std::string send_response(const std::string& data_serialized,
                              MessageType        type,
                              SendMode           send_mode,
                              MessageStatus      status,
                              const Responder&   responder) override;
    bool        needs_legacy_payload(SendMode send_mode, const Responder& responder) const;

    // Backward-compat overload for Responder::send_response in message_body.cpp.
    std::string send_message_send(const std::string& data_serialized,
                                  MessageType        type,
                                  SendMode           send_mode,
                                  MessageStatus      status,
                                  const Responder&   responder) {
        return send_message_send(data_serialized, {}, type, send_mode, status, responder);
    }

    template <class T>
    std::string send_message(const T&         data,
                             MessageType      type,
                             SendMode         send_mode,
                             MessageStatus    status    = MessageStatus::NoStatus,
                             const Responder& responder = Responder(nullptr)) {
        bool check = send_message_checker(type, send_mode, status, responder);
        if (!check) {
            return "";
        }

        // Serialize the payload in the current wire format (see WireFormat::wire()).
        // Forced via an explicit scope so it never depends on whatever ambient
        // scope a receive handler may have left active on this thread.
        std::string data_serialized;
        {
            WireFormat::Scope scope(WireFormat::wire());
            data_serialized = MessagePack::serialize(data);
        }
        // TEMPORARY 0.26 legacy compat: hex variant for pre-0.26 peers, picked
        // per-peer by send_message_connections. Drop with the wire() shim.
        std::string data_serialized_legacy;
        if (needs_legacy_payload(send_mode, responder)) {
            WireFormat::Scope scope(WireFormat::Mode::Legacy);
            data_serialized_legacy = MessagePack::serialize(data);
        }
        auto message_id =
            send_message_send(data_serialized, data_serialized_legacy, type, send_mode, status, responder);
        return message_id;
    }

    template <class T>
    std::string send_broadcast(const T& data, MessageType type, MessageStatus status = MessageStatus::NoStatus) {
        auto message_id = send_message(data, type, SendMode::Broadcast, status);
        return message_id;
    }

    SafePtr<std::map<NetworkReconnect, std::string>> reconnections();

    CalculateTraffic* calculate_traffic() const;
    std::string       public_ip() const;
    void              set_public_ip(const std::string& newPublic_ip);

    [[nodiscard]] ActorId     local_network_id() const override;
    void                      adopt_network_id(const ActorId& network_id) override;
    [[nodiscard]] std::string local_node_identifier() const override;
    [[nodiscard]] DfsMode     local_dfs_mode() const override;
    [[nodiscard]] bool has_active_duplicate(std::string_view identifier, const SocketService* candidate) override;
    [[nodiscard]] int  active_peer_count() const override;
    [[nodiscard]] int  peer_limit() const override;
    [[nodiscard]] std::set<PeerConnection> shareable_peers(std::string_view remote_ip) const override;
    void peer_authenticated(std::string_view identifier, std::string_view public_ip) override;
    [[nodiscard]] std::uint16_t local_server_port() const override;
    [[nodiscard]] bool          peer_processing_enabled() const override;
};
