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

#include "chain/actor_index.h"
#include "chain/dag.h"
#include "dfs/dfs_service.h"
#include "managers/data_mining_manager.h"
#include "core/extrachain_node.h"
#include "managers/luminance_manager.h"
#include "network/network_service.h"
#include "network/websocket_service.h"
#include "utils/exc_logs.h"
#include "dfs/historical_collection.h"
#include "dfs/dirs_manager.h"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <vector>

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/ip/address.hpp>

namespace {
    constexpr std::int64_t LIVE_DAG_QUEUE_MAX_BYTES        = 256 * 1024;
    constexpr long         LIVE_DAG_QUEUE_MAX_MESSAGES     = 256;
    constexpr std::size_t  LIVE_DAG_BATCH_MAX_TRANSACTIONS = 64;
    constexpr std::size_t  LIVE_DAG_BATCH_MAX_BYTES        = 128 * 1024;
    constexpr int          LIVE_DAG_BATCH_DELAY_MS         = 5;

    bool is_public_address(std::string_view value) {
        boost::system::error_code error;
        const auto                address = boost::asio::ip::make_address(value, error);
        if (error || address.is_unspecified() || address.is_loopback() || address.is_multicast()) {
            return false;
        }
        if (address.is_v6()) {
            const auto ipv6         = address.to_v6();
            const auto bytes        = ipv6.to_bytes();
            const bool site_local   = bytes[0] == 0xfeU && (bytes[1] & 0xc0U) == 0xc0U;
            const bool unique_local = (bytes[0] & 0xfeU) == 0xfcU;
            return !ipv6.is_link_local() && !site_local && !unique_local;
        }

        const auto bytes = address.to_v4().to_bytes();
        return bytes[0] != 10 && bytes[0] != 127 && !(bytes[0] == 169 && bytes[1] == 254)
               && !(bytes[0] == 172 && bytes[1] >= 16 && bytes[1] <= 31) && !(bytes[0] == 192 && bytes[1] == 168);
    }
} // namespace

SafePtr<std::set<SocketService::Ptr>> NetworkService::connections() const {
    return connections_;
}

std::optional<PeerMeta> NetworkService::peer_meta_for(const std::string &identifier) const {
    auto locked = *connections_;
    for (const auto &svc : *locked) {
        if (svc->identifier() == identifier) {
            return svc->peer_meta();
        }
    }
    return std::nullopt;
}

bool NetworkService::server_status(Network::Protocol protocol) const {
    switch (protocol) {
    case Network::Protocol::Udp:
        break;
    case Network::Protocol::WebSocket:
        return network_runtime_ && network_runtime_->listening();
    case Network::Protocol::Undefined:
        return false;
    }
    return false;
}

void NetworkService::connect_network() {
    if (!Utils::vector_contains(first_nodes_, first_node_)) {
        request_connection(first_node_);
        return;
    }

    if (first_node_probe_active_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    probe_first_node_candidate(0);
}

void NetworkService::probe_first_node_candidate(std::size_t index) {
    if (offline_ || index >= first_nodes_.size()) {
        first_node_probe_active_.store(false, std::memory_order_release);
        if (!offline_) {
            eLog("[Network] First nodes unavailable, waiting for next retry...");
        }
        return;
    }

    const auto address = first_nodes_[index];
    network_runtime_->async_probe(address,
                                  ws_port_,
                                  std::chrono::milliseconds(1600),
                                  [this, address, index](bool connected, std::string) {
                                      dispatch_serial([this, address, index, connected] {
                                          if (offline_) {
                                              first_node_probe_active_.store(false, std::memory_order_release);
                                              return;
                                          }
                                          if (!connected) {
                                              probe_first_node_candidate(index + 1);
                                              return;
                                          }

                                          first_node_probe_active_.store(false, std::memory_order_release);
                                          eLog("[Network] Reconnect to first node candidate {}", address);
                                          save_first_node(address);
                                          request_connection(address);
                                      });
                                  });
}

void NetworkService::dispatch_serial(std::function<void()> handler) {
    if (!handler || stopping_.load(std::memory_order_acquire)) {
        return;
    }
    boost::asio::dispatch(serial_executor_, [this, handler = std::move(handler)]() mutable {
        if (!stopping_.load(std::memory_order_acquire)) {
            handler();
        }
    });
}

SafePtr<std::map<NetworkReconnect, std::string>> NetworkService::reconnections() {
    return reconnections_to_identifier_;
}
CalculateTraffic *NetworkService::calculate_traffic() const {
    return calculate_traffic_;
}

NetworkService::SocketActivatedEvent &NetworkService::socket_activated_event() noexcept {
    return socket_activated_event_;
}

ExtraChain::Core::Event<> &NetworkService::socket_ready_event() noexcept {
    return socket_ready_event_;
}

NetworkService::ConnectionStateEvent &NetworkService::connection_state_event() noexcept {
    return connection_state_event_;
}

NetworkService::ConnectionErrorEvent &NetworkService::connection_error_event() noexcept {
    return connection_error_event_;
}

NetworkService::CustomMessageEvent &NetworkService::custom_message_event() noexcept {
    return custom_message_event_;
}

std::string NetworkService::public_ip() const {
    return public_ip_;
}

void NetworkService::set_public_ip(const std::string &new_public_ip) {
    if (new_public_ip.empty()) {
        return;
    }

    if (!is_public_address(new_public_ip)) {
        return;
    }

    public_ip_ = new_public_ip;

#if defined(__linux__) && !defined(__ANDROID__)
    return;
#endif

    if (node->public_ip_and_country_value().first.empty()) {
        node->set_public_ip_and_country(public_ip_, "Security");
    }
}

ActorId NetworkService::local_network_id() const {
    return node->actor_index()->network_id();
}

void NetworkService::adopt_network_id(const ActorId &network_id) {
    node->actor_index()->set_network_id(network_id);
}

std::string NetworkService::local_node_identifier() const {
    return node->node_identifier();
}

DfsMode NetworkService::local_dfs_mode() const {
    return node->dfs_service()->mode();
}

bool NetworkService::has_active_duplicate(std::string_view identifier, const SocketService *candidate) {
    auto locked = *connections_;
    for (const auto &connection : *locked) {
        if (connection.get() == candidate || connection->identifier() != identifier) {
            continue;
        }
        if (connection->is_active()) {
            return true;
        }
        connection->close_connection();
        return false;
    }
    return false;
}

int NetworkService::active_peer_count() const {
    int  count  = 0;
    auto locked = *connections_;
    for (const auto &connection : *locked) {
        if (connection->is_active()) {
            ++count;
        }
    }
    return count;
}

int NetworkService::peer_limit() const {
    return max_connections();
}

std::set<PeerConnection> NetworkService::shareable_peers(std::string_view remote_ip) const {
    std::set<PeerConnection> result;
    auto                     locked = *connections_;
    for (const auto &connection : *locked) {
        const auto &connection_ip = connection->ip();
        if (connection_ip.empty() || connection_ip == remote_ip || connection_ip == "127.0.0.1"
            || !connection->is_active()) {
            continue;
        }
        result.insert(PeerConnection { connection_ip, connection->identifier() });
    }
    return result;
}

void NetworkService::peer_authenticated(std::string_view identifier, std::string_view public_ip) {
    Responder responder(this);
    responder.add_identifier(std::string(identifier));
    node->actor_index()->send_system_actor(responder);
    set_public_ip(std::string(public_ip));
}

std::uint16_t NetworkService::local_server_port() const {
    return ws_port_;
}

bool NetworkService::peer_processing_enabled() const {
    return node_enabled.load(std::memory_order_acquire);
}

NetworkService::NetworkService(ExtraChain::Core::ExtraChainNode *node,
                               ExtraChain::Core::NetworkRuntime &runtime,
                               std::uint16_t                     port)
    : node(node)
    , network_runtime_(&runtime)
    , serial_executor_(node->serial_executor())
    , ws_port_(port) {
    if (!first_nodes_.empty()) {
        first_node_ = first_nodes_.front();
    }

    local_inizialization();
    initialize_first_node();

    reconnect_timer_             = ExtraChain::Core::DeadlineTask::create(serial_executor_, [this] {
        if (!stopping_.load(std::memory_order_acquire)) {
            reconnection();
        }
    });
    clear_network_caches_timer_  = ExtraChain::Core::DeadlineTask::create(serial_executor_, [this] {
        if (!stopping_.load(std::memory_order_acquire)) {
            clear_network_caches();
        }
    });
    live_dag_batch_timer_        = ExtraChain::Core::DeadlineTask::create(serial_executor_, [this] {
        if (!stopping_.load(std::memory_order_acquire)) {
            flush_live_dag_batches();
        }
    });
    calculate_traffic_           = CalculateTraffic::get_instance();
    runtime_activity_connection_ = node->runtime_activity_event().subscribe([this](RuntimeActivity activity) {
        if (this->node->runtime_profile() == RuntimeProfile::FullNode) {
            return;
        }

        if (activity == RuntimeActivity::Background) {
            std::vector<std::pair<int, SocketService::Ptr>> active_services;
            {
                auto locked = *connections_;
                for (const auto &service : *locked) {
                    if (!service || !service->is_active()) {
                        continue;
                    }

                    int score = 0;
                    if (service->is_constant()) {
                        score = 1;
                    }
                    if (service->ip() == first_node_) {
                        score = 2;
                    }
                    active_services.emplace_back(score, service);
                }
            }

            std::stable_sort(active_services.begin(),
                             active_services.end(),
                             [](const auto &left, const auto &right) {
                                 return left.first > right.first;
                             });
            const auto keep_count = this->node->runtime_limits().peer_limit;
            for (std::size_t index = keep_count; index < active_services.size(); ++index) {
                if (active_services[index].second) {
                    active_services[index].second->close_connection();
                }
            }
        }
        schedule_reconnection(activity == RuntimeActivity::Background ? 60000 : 1000);
    });

    process();
}

ExtraChain::Core::NetworkStatus &NetworkService::network_status() noexcept {
    return network_status_;
}

void NetworkService::add_all_services_identifiers_to_message(MessageBody &msg) {
    for (const auto &it : msg.nodes_identifiers_to_ignore_later) {
        msg.nodes_identifiers_to_ignore.emplace(it);
    }
    msg.nodes_identifiers_to_ignore_later.clear();

    msg.nodes_identifiers_to_ignore_later.emplace(node->node_identifier());

    auto connectionsLocked = *connections_;
    for (const auto &service : *connectionsLocked) {
        std::string ident = service->identifier();

        if (!ident.empty())
            msg.nodes_identifiers_to_ignore_later.emplace(ident);
    }
}

bool NetworkService::is_first_node(const std::string &identifier) {
    auto connectionsLocked = *connections_;
    if (connectionsLocked->empty()) {
        return false;
    }

    for (const auto &el : *connectionsLocked) {
        if (el->identifier() == identifier && el->ip() == first_node_) {
            return true;
        }
    }

    return false;
}

void NetworkService::process() {
    // Full nodes also re-dial, but only towards peers already proven reachable:
    // their configured first_node (uplink) and entries in reconn_, which is
    // populated exclusively from sockets that reached SocketMode::Full (i.e. real
    // servers we connected out to). A client behind NAT/router never lands there,
    // so this never spams unreachable clients — it only restores a lost uplink so
    // the node keeps replicating instead of silently falling out of the mesh.
    // The reconnection() slot also guards against a seed whose first_node is self.
    schedule_reconnection(Utils::RECONNECT_INTERVAL);
}

void NetworkService::schedule_reconnection(int delay_ms) {
    if (delay_ms < 0) {
        return;
    }
    reconnect_timer_->schedule_earlier(std::chrono::milliseconds(delay_ms));
}

void NetworkService::go_offline() {
    offline_ = true;
    first_node_probe_active_.store(false, std::memory_order_release);
    reconnect_timer_->cancel();
    live_dag_batch_timer_->cancel();
    live_dag_peer_queues_.clear();
    {
        auto reconnectionsLocked = *reconnections_to_identifier_;
        reconnectionsLocked->clear();
    }
    auto connectionsLocked = *connections_;
    for (const auto &connection : *connectionsLocked) {
        connection->close_connection();
    }
    eWarning("[NetworkService] offline mode: all connections closed, reconnects disabled");
}

bool NetworkService::is_own_address(const std::string &ip) const {
    if (ip.empty()) {
        return false;
    }
    boost::system::error_code error;
    const auto                target = boost::asio::ip::make_address(ip, error);
    if (error) {
        return false;
    }
    // A first_node equal to one of this host's own interface addresses means this
    // node is the seed. Loopback is deliberately NOT treated as "self" here: on a
    // single-host test mesh several nodes share 127.0.0.x and must still dial each
    // other; a genuine self-loop there is caught by the network-id check instead.
    return !target.is_loopback() && !local_ip_.empty() && target.to_string() == local_ip_;
}

void NetworkService::reconnection() {
    if (offline_) {
        return;
    }
    // Do not dial ourselves: the network seed's first_node resolves to one of this
    // host's own listening addresses. A self-connection would pass the handshake
    // (matching network id) and loop, so skip re-dial entirely for the seed.
    if (server_status() && is_own_address(first_node_)) {
        return;
    }
    if (first_node_self_detected_.load()) {
        return;
    }
    if (this->node->account_controller()->empty()) {
        schedule_reconnection(30000);
        return;
    }

    if (this->failed_ips_.contains(this->first_node_)) {
        schedule_reconnection(60000);
        return;
    }

    // if (first_node_ == localIp().toStdString()) {
    //     m_reconnectTimer->stop();
    //     return;
    // }

    bool                         skip_first_node = false;
    auto                         need_reconnect  = this->reconn_;
    std::set<SocketService::Ptr> to_close;

    {
        auto connectionsLocked = *this->connections_;
        for (const auto &el : *connectionsLocked) {
            // eLog("_____________");
            if (el->is_closed()) {
                // if (Utils::current_date_ms() - el->timestamp() > 10000) {
                //     s.insert(el);
                // }
                continue;
            }

            if (el->ip() == this->first_node_) {
                bool is_early = Utils::current_date_ms() - el->timestamp() < 30000;

                if (!is_early && !el->is_active()) {
                    skip_first_node = false;
                    to_close.insert(el);
                    break;
                } else {
                    skip_first_node = true;
                }
            }

            if (el->timestamp() != 0 && need_reconnect.contains(el->ip())) {
                need_reconnect.erase(el->ip());
            }

            if (el->timestamp() != 0 && !el->is_active() && Utils::current_date_ms() - el->timestamp() > 30000) {
                // eLog("PHYYYY {}", Utils::current_date_ms() - el->timestamp());
                // to_close.insert(el);
            }
        }
    }

    for (const auto &el : to_close) {
        el->close_connection();
    }

    if (!skip_first_node) {
        this->connect_network();
        schedule_reconnection(10000);
        return;
    }

    const std::int64_t now = Utils::current_date_ms();
    for (auto &[ip, entry] : reconn_) {
        if (!need_reconnect.contains(ip))
            continue;
        if (this->failed_ips_.contains(ip))
            continue;
        if (now < entry.next_attempt_ms)
            continue;

        eLog("[Network] Reconnect to node: {} (attempt {})", ip, entry.attempts + 1);
        request_connection(ip);

        if (entry.attempts < 7)
            entry.attempts++;
#ifdef IS_APP_UI_CLIENT
        constexpr int max_delay_ms = 60'000;
#else
        constexpr int max_delay_ms = 300'000;
#endif
        const int delay       = std::min(5000 * (1 << entry.attempts), max_delay_ms);
        entry.next_attempt_ms = now + delay;
    }

    int next_delay_ms = node->runtime_activity() == RuntimeActivity::Background ? 60000 : 30000;
    for (const auto &[ip, entry] : reconn_) {
        if (!need_reconnect.contains(ip) || failed_ips_.contains(ip)) {
            continue;
        }
        next_delay_ms =
            std::min(next_delay_ms, static_cast<int>(std::max<std::int64_t>(1000, entry.next_attempt_ms - now)));
    }
    schedule_reconnection(next_delay_ms);
}

void NetworkService::connectWsService(const std::shared_ptr<WebSocketService> &service, bool requestListNodes) {
    (void)requestListNodes;
    service->on_error = [this](SocketService::Ptr,
                               Network::SocketServiceError error,
                               const std::string          &error_data,
                               const std::string          &ip,
                               const std::string          &identifier,
                               SocketDirection             direction) {
        dispatch_serial([this, error, error_data, ip, identifier, direction] {
            socket_error(error, error_data, ip, identifier, direction);
        });
    };
    service->on_disconnected = [this](SocketService::Ptr disconnected) {
        const std::weak_ptr<SocketService> weak = disconnected;
        dispatch_serial([this, weak] {
            const auto connection = weak.lock();
            if (connection) {
                remove_socket_connection(connection);
            }
        });
    };
    service->on_activated = [this](SocketService::Ptr activated) {
        const std::weak_ptr<SocketService> weak = activated;
        dispatch_serial([this, weak] {
            const auto activated = weak.lock();
            if (!activated) {
                return;
            }
            check_connections_status();
            socket_activated_event_.publish(activated->ip(), activated->identifier());
            socket_ready_event_.publish();

            if (activated->mode() == SocketMode::Full && activated->ip() != first_node()
                && activated->direction() == SocketDirection::Outgoing) {
                reconn_.insert({ activated->ip(), {} });
            }
        });
    };
    service->on_message = [this](SocketService::Ptr, std::string message, std::string ip, std::string identifier) {
        dispatch_serial(
            [this, message = std::move(message), ip = std::move(ip), identifier = std::move(identifier)] {
                message_received(message, ip, identifier);
            });
    };
    service->on_share_connections = [this](SocketService::Ptr,
                                           const std::set<SocketService::SocketPair> &connections) {
        dispatch_serial([this, connections] {
            const auto &init_ip = node->public_ip_and_country_value().first;
            if (active_connections_count() >= max_connections()) {
                eLog("shareConnections ignored by max connections limit");
                if (init_ip != first_node_) {
                    return;
                }
            }

            for (const auto &[ip, identifier] : connections) {
                if (failed_ips_.contains(ip) || ip == "127.0.0.1") {
                    continue;
                }

                bool can_connect = ip != init_ip && identifier != node->node_identifier();
                if (can_connect) {
                    auto locked = *connections_;
                    for (const auto &current : *locked) {
                        if (current->identifier() == identifier || current->ip() == ip) {
                            can_connect = false;
                            break;
                        }
                    }
                }
                if (can_connect) {
                    request_connection(ip);
                }
            }
        });
    };

    auto locked = *connections_;
    locked->insert(service);
}

void NetworkService::disconnect_peer(std::string identifier) {
    if (identifier.empty()) {
        eFatal("Try remove with empty identifier");
    }
    dispatch_serial([this, identifier = std::move(identifier)] {
        auto connections_locked = *connections_;
        for (const auto &connection : *connections_locked) {
            if (connection->identifier() == identifier) {
                connection->close_connection();
            }
        }
    });
}

void NetworkService::request_connection(std::string       ip,
                                        Network::Protocol protocol,
                                        bool              request,
                                        bool              is_constant,
                                        bool              is_light) {
    dispatch_serial([this, ip = Utils::trim(ip), protocol, request, is_constant, is_light]() mutable {
        check_port(std::move(ip), protocol, request, is_constant, is_light);
    });
}

void NetworkService::check_port(std::string       ip,
                                Network::Protocol protocol,
                                bool              request,
                                bool              is_constant,
                                bool              is_light) {
    if (offline_) {
        return;
    }
    // Limit already reached — no need for new probes (the app used to handshake the entire
    // node list, killing dozens of excess sockets right after creation).
    if (active_connections_count() >= Network::maxConnections) {
        return;
    }

    // Also cap in-flight candidates, but with headroom: not all nodes hold the network's DFS
    // data, so too narrow a search leaves us with neighbours lacking the needed files
    // (normal responses become Unknown, and files don't download).
    static std::atomic_int pending_probes { 0 };
    if (pending_probes.load() + active_connections_count() >= Network::maxConnections * 5) {
        return;
    }
    pending_probes.fetch_add(1);
    struct PendingProbe final {
        explicit PendingProbe(std::atomic_int &value)
            : count(&value) {
        }
        PendingProbe(const PendingProbe &)            = delete;
        PendingProbe &operator=(const PendingProbe &) = delete;

        ~PendingProbe() {
            count->fetch_sub(1, std::memory_order_acq_rel);
        }

        std::atomic_int *count;
    };
    const auto pending_probe = std::make_shared<PendingProbe>(pending_probes);

    network_runtime_->async_probe(ip,
                                  ws_port_,
                                  std::chrono::seconds(3),
                                  [this,
                                   ip = std::move(ip),
                                   protocol,
                                   request,
                                   is_constant,
                                   is_light,
                                   pending_probe](bool connected, std::string) {
                                      if (!connected) {
                                          return;
                                      }
                                      dispatch_serial([this, ip, protocol, request, is_constant, is_light] {
                                          connect_node(ip, protocol, request, is_constant, is_light);
                                      });
                                  });
}

NetworkService::~NetworkService() {
    prepare_shutdown();
    network_runtime_->stop();
}

void NetworkService::prepare_shutdown() {
    if (stopping_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    runtime_activity_connection_.disconnect();
    network_status_connection_.disconnect();

    eLog("[NetworkService] Finish him with {} connections", connections_->size());

    reconnect_timer_->cancel();
    clear_network_caches_timer_->cancel();
    live_dag_batch_timer_->cancel();
    live_dag_peer_queues_.clear();
    network_runtime_->stop_listening();

    std::set<SocketService::Ptr> copied;
    {
        auto connectionsLocked = *connections_;
        copied                 = **connections_;
    }

    for (const auto &connection : copied) {
        connection->on_error             = {};
        connection->on_disconnected      = {};
        connection->on_activated         = {};
        connection->on_share_connections = {};
        connection->on_message           = {};
        connection->flush();
        connection->close_connection();
    }
    const auto close_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    for (const auto &connection : copied) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            close_deadline - std::chrono::steady_clock::now());
        if (remaining <= std::chrono::milliseconds::zero() || !connection->wait_closed(remaining)) {
            eWarning("[NetworkService] Timed out while closing {}", connection->ip());
        }
    }
    {
        auto locked = *connections_;
        locked->clear();
    }
}

void NetworkService::check_connections_status() {
    std::unordered_set<std::string> ind_temp;
    // m_reconnectTimer->stop();
    bool flag  = false;
    int  count = 0;
    {
        auto connectionsLocked = *connections_;
        std::for_each(connectionsLocked->begin(), connectionsLocked->end(), [&](const SocketService::Ptr &el) {
            flag = flag || el->is_active();
            if (el->is_active()) {
                count++;
                ind_temp.insert(el->identifier());
            }
        });
    }
    connection_state_event_.publish(flag, count);
}

void NetworkService::start_network() {
    eLog("[NetworkService] Start servers... {}", (ws_port_ == 17593 ? "Network" : "Else"));

    if (node->runtime_profile() == RuntimeProfile::MobileLight) {
        eLog("[NetworkService] Inbound server is disabled for the mobile light profile");
        return;
    }

    if (local_ip_.empty()) {
        eWarning("[NetworkService] Local address is unavailable; listener will use the wildcard address");
    }

    if (!Network::isStartedServer)
        return;

    if (network_runtime_->listening()) {
        return;
    }

    const auto result =
        network_runtime_->listen(ExtraChain::Core::NetworkConfig { .bind_address = node->bind_address(),
                                                                   .port         = ws_port_ },
                                 [this](ExtraChain::Core::NetworkRuntime::Tcp::socket socket) {
                                     if (active_connections_count() >= max_connections()) {
                                         boost::system::error_code error;
                                         socket.close(error);
                                         return;
                                     }

                                     auto service = WebSocketService::from_accepted(*network_runtime_,
                                                                                    std::move(socket),
                                                                                    *this);
                                     service->set_direction(SocketDirection::Incoming);
                                     connectWsService(service);
                                     network_runtime_->spawn(service->run(true));
                                 });
    if (!result.has_value()) {
        eWarning("[NetworkService] Can't listen on port {}: {}", ws_port_, result.error());
        return;
    }

    eLog("[WS] Start listening on port {}", ws_port_);
}

boost::asio::awaitable<void> NetworkService::connect_websocket(std::string   ip,
                                                               std::uint16_t port,
                                                               bool          request_list_nodes,
                                                               bool          is_constant,
                                                               bool          is_light) {
    auto result = co_await WebSocketService::connect(*network_runtime_, ip, port, *this, is_constant, is_light);
    if (stopping_.load(std::memory_order_acquire)) {
        co_return;
    }
    if (!result.has_value()) {
        const auto detail = result.error();
        dispatch_serial([this, ip = std::move(ip), detail] {
            socket_error(Network::SocketServiceError::Unknown, detail, ip, {}, SocketDirection::Outgoing);
        });
        co_return;
    }

    auto service = std::move(result.value());
    service->set_direction(SocketDirection::Outgoing);
    connectWsService(service, request_list_nodes);
    dispatch_serial([this, ip, port] {
        reconnections_to_identifier_
            ->emplace(NetworkReconnect { .ip = ip, .port = port, .protocol = Network::Protocol::WebSocket }, "");
    });
    co_await service->run(false);
}

void NetworkService::connect_node(std::string       ip,
                                  Network::Protocol protocol,
                                  bool              request,
                                  bool              is_constant,
                                  bool              is_light) {
    if (ip == first_node_) {
        is_constant = true;
    }

    if (active_connections_count() >= max_connections()) {
        if (is_constant && !remove_one_connection()) {
            eLog("[NetworkService] Can't connect because the maximum number of connections");
            return;
        }
    }

    if (ip.empty()) {
        return;
    }

    const std::uint16_t port = (protocol == Network::Protocol::WebSocket ? ws_port_ : 0);
    eLog("[NetworkService] Connect to {}, protocol: {}, port: {}", ip, Utils::enum_value_name(protocol), port);

    using Network::Protocol;
    switch (protocol) {
    case Protocol::Udp:
        break;
    case Protocol::WebSocket:
        connect_to_websocket(std::move(ip), port, request, is_constant, is_light);
        break;
    case Protocol::Undefined:
        eFatal("Undefined connectToNode");
    }
}

void NetworkService::request_endpoint(std::string   ip,
                                      std::uint16_t port,
                                      bool          request_list_nodes,
                                      bool          is_constant,
                                      bool          is_light) {
    dispatch_serial([this, ip = Utils::trim(ip), port, request_list_nodes, is_constant, is_light]() mutable {
        if (ip.empty() || port == 0) {
            eWarning("[NetworkService] Invalid endpoint {}:{}", ip, port);
            return;
        }
        if (active_connections_count() >= Network::maxConnections && (!is_constant || !remove_one_connection())) {
            eWarning("[NetworkService] Can't connect to {}:{}: maximum connections reached", ip, port);
            return;
        }
        eLog("[NetworkService] Connect to endpoint {}:{}", ip, port);
        connect_to_websocket(std::move(ip), port, request_list_nodes, is_constant, is_light);
    });
}

void NetworkService::connect_to_websocket(std::string   ip,
                                          std::uint16_t port,
                                          bool          request_list_nodes,
                                          bool          is_constant,
                                          bool          is_light) {
    if (ip.empty()) {
        return;
    }

    {
        std::vector<SocketService::Ptr> to_close;
        auto                            connectionsLocked = *connections_;
        for (const auto &el : *connectionsLocked) {
            if (el->ip() == ip) {
                if (el->is_active()) {
                    return;
                }
                if (!el->is_closed()) {
                    to_close.push_back(el);
                }
            }
        }
        for (const auto &el : to_close) {
            el->close_connection();
        }
    }

    network_runtime_->spawn(connect_websocket(std::move(ip), port, request_list_nodes, is_constant, is_light));
}

void NetworkService::clear_network_caches() {
    const auto now = std::chrono::steady_clock::now();
    {
        auto network_forwarded_messages_locked = *forwarded_messages_;
        for (auto it = network_forwarded_messages_locked->begin();
             it != network_forwarded_messages_locked->end();) {
            if (now - it->second.second >= std::chrono::minutes(2)) {
                it = network_forwarded_messages_locked->erase(it);
            } else
                ++it;
        }
    }

    {
        auto messages_locked = *messages_;
        for (auto it = messages_locked->begin(); it != messages_locked->end();) {
            if (now - it->second.second >= std::chrono::minutes(2)) {
                // eTemp("MessageID erased: {}", it->first);
                it = messages_locked->erase(it);
            } else
                ++it;
        }
    }

    {
        for (auto it = msg_hash_list_.begin(); it != msg_hash_list_.end();) {
            if (now - it->second.second >= std::chrono::minutes(2)) {
                it = msg_hash_list_.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (!forwarded_messages_->empty() || !messages_->empty() || !msg_hash_list_.empty()) {
        schedule_cache_cleanup();
    }
}

void NetworkService::schedule_cache_cleanup() {
    if (!clear_network_caches_timer_->active()) {
        clear_network_caches_timer_->schedule_after(std::chrono::minutes(2));
    }
}

bool NetworkService::send_message_checker(MessageType      type,
                                          SendMode         send_mode,
                                          MessageStatus    status,
                                          const Responder &responder) {
    if (/*send_mode != SendMode::Broadcast && */ status == MessageStatus::Response
        && responder.message_id().empty() && responder.identifiers().empty()) {
        eCritical("[Network] Send message error: empty message id or receiver identifiers for response message");
        return false;
    }
    if (!node) {
        eCritical("[Network] Send message error: accountController is bye 1!");
        return false;
    }
    if (!node->account_controller()) {
        eCritical("[Network] Send message error: accountController is bye 2!");
        return false;
    }
    if (node->account_controller()->empty()) {
        eCritical("[Network] Send message error: accountController is empty!");
        return false;
    }
    if (/*send_mode != SendMode::Broadcast && */ status == MessageStatus::Response
        && send_mode != SendMode::Focused) {
        eWarning(
            "[Network] Send message warning: incorrect type send for response message, set to focused, "
            "type: "
            "{}",
            type);
        send_mode = SendMode::Focused;
    }

    return true;
}

std::string NetworkService::send_response(const std::string &data_serialized,
                                          MessageType        type,
                                          SendMode           send_mode,
                                          MessageStatus      status,
                                          const Responder   &responder) {
    if (!send_message_checker(type, send_mode, status, responder)) {
        return {};
    }
    return send_message_send(data_serialized, type, send_mode, status, responder);
}

std::string NetworkService::send_message_send(const std::string &data_serialized,
                                              const std::string &data_serialized_legacy,
                                              MessageType        type,
                                              SendMode           send_mode,
                                              MessageStatus      status,
                                              const Responder   &responder) {
    auto &main_actor = node->account_controller()->system_actor();

    // Build two parallel outer envelopes: canonical (decimal wire) and legacy (hex wire).
    // Outer MessageBody carries the inner payload as-is plus signature; it has to be
    // built and signed separately per variant so the signed hash matches the bytes
    // that actually hit the network.
    MessageBody message        = make_init_message(data_serialized,
                                            send_mode,
                                            type,
                                            status,
                                            main_actor.id(),
                                            responder.message_id(),
                                            node->node_identifier());
    const bool  legacy_needed  = !data_serialized_legacy.empty();
    MessageBody message_legacy = message;
    if (legacy_needed)
        message_legacy.data = data_serialized_legacy;

    if (send_mode == SendMode::Broadcast) {
        this->add_all_services_identifiers_to_message(message);
        // Reuse broadcast identifiers in the legacy envelope.
        message_legacy.nodes_identifiers_to_ignore = message.nodes_identifiers_to_ignore;
    }

    auto sign_envelope = [&main_actor](MessageBody &m) -> std::optional<std::string> {
        auto serialized      = m.serialize();
        auto serialized_hash = m.calculate_hash();
        auto sign_result     = main_actor.key().sign(ByteArray(serialized_hash).toBytes());
        if (!sign_result.has_value())
            return std::nullopt;
        auto sign = ByteArray(sign_result.value()).toString();
        return serialized + sign;
    };

    auto canonical_blob = sign_envelope(message);
    auto legacy_blob    = legacy_needed ? sign_envelope(message_legacy) : canonical_blob;
    if (!canonical_blob.has_value() || !legacy_blob.has_value()) {
        return "";
    }

    std::string to_message_id = responder.message_id();
    std::string receiver_identifier;
    if (!to_message_id.empty()) {
        auto messages_locked = *messages_;
        if (messages_locked->count(to_message_id)) {
            receiver_identifier = messages_locked->at(to_message_id).first;
        }
    }

    if (!responder.identifiers().empty()) {
        receiver_identifier = *responder.identifiers().begin();
    }

#ifndef NDEBUG
    if (Network::networkDebug) {
        auto                   serialized   = message.serialize();
        msgpack::object_handle oh           = msgpack::unpack(serialized.data(), serialized.size());
        msgpack::object        deserialized = oh.get();
        eLog("[Network Message] Send: type {}, status {}, id {}, type send {}, body: {}",
             message.message_type,
             message.status,
             message.message_id,
             send_mode,
             (std::stringstream() << deserialized).str());
    }
#endif

    this->send_message_connections(*canonical_blob,
                                   *legacy_blob,
                                   message,
                                   send_mode,
                                   receiver_identifier,
                                   type,
                                   status);

    return message.message_id;
}

bool NetworkService::needs_legacy_payload(SendMode send_mode, const Responder &responder) const {
    if (WireFormat::wire() == WireFormat::Mode::Legacy)
        return false;

    std::string focused_identifier;
    if (!responder.identifiers().empty()) {
        focused_identifier = *responder.identifiers().begin();
    } else if (!responder.message_id().empty()) {
        auto messages_locked = *messages_;
        auto found           = messages_locked->find(responder.message_id());
        if (found != messages_locked->end())
            focused_identifier = found->second.first;
    }

    auto connections_locked = *connections_;
    for (const auto &service : *connections_locked) {
        if (service == nullptr || !service->is_active())
            continue;
        if (send_mode == SendMode::Focused && service->identifier() != focused_identifier) {
            continue;
        }
        if (service->peer_meta().is_legacy_dag())
            return true;
    }
    return false;
}

void NetworkService::send_message_connections(const std::string &serialized_message,
                                              const std::string &serialized_message_legacy,
                                              const MessageBody &non_serialized_message,
                                              SendMode           send_mode,
                                              const std::string &receiver_identifier,
                                              MessageType        message_type,
                                              MessageStatus      status_info,
                                              PeerSelection      peer_selection) {
    if (!is_active_connection_exists()) {
        // Cache canonical payload — if connection later comes back, peer version is unknown.
        save_to_cache(serialized_message, send_mode, receiver_identifier);
        return;
    }

    auto payload_for = [&](const SocketService::Ptr &s) -> const std::string & {
        return s->peer_meta().is_legacy_dag() ? serialized_message_legacy : serialized_message;
    };

    static auto is_send_check = [](const SendMode    &type_send,
                                   const std::string &receiver_identifier,
                                   const std::string &socket_identifier,
                                   const MessageBody &package) {
        switch (type_send) {
        case SendMode::Except:
            return socket_identifier != receiver_identifier;
        case SendMode::Focused:
            return socket_identifier == receiver_identifier;
        case SendMode::Neighbours:
            return true;
        case SendMode::Broadcast: {
            bool res = !package.nodes_identifiers_to_ignore.contains(socket_identifier);

            if (res) {
                // eTemp("[Network] Broadcast further to socket: {}", socket_identifier);
            }
            return res;
        }
        default:
            return false;
        }
    };

    SocketService::Priority priority = SocketService::Priority::Normal;

    if (message_type == MessageType::DfsFileExistNotification || message_type == MessageType::DfsFileFragment
        || message_type == MessageType::Actors || message_type == MessageType::DfsSyncDirRows) {
        priority = SocketService::Priority::Low;
    }

    const bool high_priority_dag_sync =
        message_type == MessageType::DagSections || message_type == MessageType::DagLightData
        || message_type == MessageType::DagFileSections || message_type == MessageType::DagPackData
        || message_type == MessageType::DagCacheSnapshotData;
    if (message_type == MessageType::Custom || message_type == MessageType::NewActor
        || message_type == MessageType::DagTransactionResult || message_type == MessageType::DagIntervalHash
        || message_type == MessageType::DagSyncLastInfo || message_type == MessageType::DagControlRangeRequest
        || message_type == MessageType::DagControlRangeResponse || message_type == MessageType::DagPackList
        || message_type == MessageType::DagPackRequest || message_type == MessageType::DagCacheSnapshotRequest
        || high_priority_dag_sync) {
        priority = SocketService::Priority::High;
    }

    auto connections_locked = *connections_;

    if (send_mode == SendMode::NeighboursRandom || send_mode == SendMode::OneNeighbourRandom) {
        std::vector<SocketService::Ptr> active_identifiers;
        const int                       randoms = send_mode == SendMode::NeighboursRandom ? 3 : 1;

        for (auto service : *connections_locked) {
            if (service->is_active()) {
                active_identifiers.push_back(service);
            }
        }

        if (active_identifiers.empty()) {
            return;
        }

        auto indexes = Utils::random_indices<3>(active_identifiers.size());
        if (send_mode == SendMode::NeighboursRandom && active_identifiers.size() > 3) {

            for (int index : indexes) {
                const auto &svc = active_identifiers[index];
                svc->send_message(payload_for(svc), priority);
            }

            return;
        }

        if (send_mode == SendMode::OneNeighbourRandom) {
            const auto &svc = active_identifiers[indexes[0]];
            svc->send_message(payload_for(svc), priority);
        }
    }

    if (serialized_message.size() > 10000
        && (non_serialized_message.message_type != MessageType::DfsFileExistNotification
            && non_serialized_message.message_type != MessageType::DfsFileFragment)) {
        eTemp("Message: BIG {} {}", serialized_message.size(), non_serialized_message.message_type);
    }

    TIMER_START(kkk)

    const bool apply_live_dag_backpressure =
        (message_type == MessageType::DagTransaction || message_type == MessageType::DagTransactionBatch)
        && send_mode != SendMode::Focused;

    int skipped_inactive = 0;
    int skipped_light    = 0;
    int sent_to          = 0;

    for (const auto &service : *connections_locked) {
        if (!service->is_active()) {
            ++skipped_inactive;
            continue;
        }

        if (service->mode() == SocketMode::Light && send_mode != SendMode::Focused) {
            ++skipped_light;
            continue;
        }

        if (peer_selection == PeerSelection::LegacyDag && !service->peer_meta().is_legacy_dag()) {
            continue;
        }

        bool send_checked =
            is_send_check(send_mode, receiver_identifier, service->identifier(), non_serialized_message);
        if (send_mode == SendMode::Broadcast && !receiver_identifier.empty()) {
            send_checked = send_checked && service->identifier() == receiver_identifier;
        }

        if (send_checked) {
            // A peer can recover a skipped live transaction through verified DAG sync.
            // Keep its socket queue available for control messages and sync data.
            if (apply_live_dag_backpressure
                && (service->pending_bytes() >= LIVE_DAG_QUEUE_MAX_BYTES
                    || service->queue_size() >= LIVE_DAG_QUEUE_MAX_MESSAGES)) {
                eWarning("[Network] Live DAG backpressure: dropping {} to {} (pending {} bytes, {} queued)",
                         message_type,
                         service->ip(),
                         service->pending_bytes(),
                         service->queue_size());
                continue;
            }

            const std::string &payload = payload_for(service);
            calculate_traffic_->add_bytes_sent(service->ip(), payload.size());
            service->send_message(payload, priority);
            ++sent_to;
            if (send_mode == SendMode::Focused) {
                break;
            }
        }
    }

    if (send_mode == SendMode::Focused && sent_to == 0) {
        // A focused message that matches no connection vanishes silently — the
        // requester keeps waiting for a response that was never written to any
        // socket. Seen with a 22 KB DagFileSections response: the sender logged the
        // serialization, the peer never received a byte, and the sync stayed stuck.
        eWarning("[Network] Focused {} reached nobody (target identifier not connected)", message_type);
    }

    if (message_type == MessageType::DagTransaction && sent_to == 0) {
        // Distinguish the two cases, because only one of them is a loss.
        //
        // A *relay* reaching nobody is normal: `nodes_identifiers_to_ignore` carries
        // "already delivered", so once every neighbour has the message there is
        // correctly nobody left to forward it to.
        //
        // Our *own* transaction reaching nobody is a silent loss. A locally created
        // transaction is not written to our chain when we send it — it waits in
        // `sended_transactions_` until a peer answers DagTransactionResult, and only
        // then is it saved (dag.cpp: network_transaction_result). If it left the node
        // for no one, that answer can never arrive: the transaction is gone, with no
        // rejection and no retry.
        // Only a locally created transaction is worth a line here: it is not written to
        // our chain when sent, it waits in `sended_transactions_` for a peer's
        // DagTransactionResult and is saved only then. Leaving the node for no one means
        // that answer can never arrive — a silent loss with no rejection and no retry.
        //
        // A relay reaching nobody is the normal end of a broadcast:
        // `nodes_identifiers_to_ignore` means "already delivered", so an empty result
        // just says every neighbour already has it. Measured: 1036 of 1036 such cases in
        // one run were relays. Counting them as losses is what made this look alarming.
        if (non_serialized_message.nodes_identifiers_to_ignore.empty()) {
            std::string peers;
            for (const auto &service : *connections_locked) {
                peers += service->identifier().substr(0, 6) + " ";
            }
            eCritical(
                "[Network] Own transaction reached nobody — it can never be approved: "
                "{} connections, {} inactive, {} light, peers=[{}]",
                connections_locked->size(),
                skipped_inactive,
                skipped_light,
                peers);
        }
    }

    const auto k =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - kkk).count();
    if (k > 5) {
        eLog("____ send {} ms {}", k, message_type);
    }
}

bool NetworkService::remember_live_dag_hash(const std::string &hash) {
    std::lock_guard lock(recent_dag_hashes_mutex_);
    if (!recent_dag_hashes_.insert(hash).second)
        return false;
    recent_dag_hash_order_.push_back(hash);
    const auto limit = node->runtime_limits().cached_transactions;
    while (recent_dag_hash_order_.size() > limit) {
        recent_dag_hashes_.erase(recent_dag_hash_order_.front());
        recent_dag_hash_order_.pop_front();
    }
    return true;
}

void NetworkService::forget_live_dag_hash(const std::string &hash) {
    std::lock_guard lock(recent_dag_hashes_mutex_);
    recent_dag_hashes_.erase(hash);
    std::erase(recent_dag_hash_order_, hash);
}

void NetworkService::queue_live_dag_transaction(const Transaction                     &transaction,
                                                const std::string                     &source_identifier,
                                                const std::unordered_set<std::string> &ignored_identifiers) {
    dispatch_serial([this, transaction, source_identifier, ignored_identifiers] {
        queue_live_dag_transaction_serial(transaction, source_identifier, ignored_identifiers);
    });
}

void NetworkService::queue_live_dag_transaction_serial(Transaction                     transaction,
                                                       std::string                     source_identifier,
                                                       std::unordered_set<std::string> ignored_identifiers) {
    std::size_t serialized_size = 0;
    {
        WireFormat::Scope scope(WireFormat::Mode::Canonical);
        serialized_size = MessagePack::serialize(transaction).size();
    }
    if (serialized_size > LIVE_DAG_BATCH_MAX_BYTES)
        return;

    bool flush_now = false;
    {
        auto connections_locked = *connections_;
        for (const auto &service : *connections_locked) {
            if (service == nullptr || !service->is_active() || service->mode() == SocketMode::Light
                || !service->peer_meta().supports_dag_tx_batch()) {
                continue;
            }
            const auto identifier = service->identifier();
            if (identifier.empty() || identifier == source_identifier || ignored_identifiers.contains(identifier))
                continue;

            auto &queue = live_dag_peer_queues_[identifier];
            if (queue.entries.size() >= static_cast<std::size_t>(LIVE_DAG_QUEUE_MAX_MESSAGES)
                || queue.bytes + serialized_size > static_cast<std::size_t>(LIVE_DAG_QUEUE_MAX_BYTES)) {
                continue;
            }
            queue.entries.push_back(LiveDagQueueEntry { transaction, serialized_size });
            queue.bytes += serialized_size;
            flush_now = flush_now || queue.entries.size() >= LIVE_DAG_BATCH_MAX_TRANSACTIONS;
        }
    }

    if (flush_now) {
        flush_live_dag_batches();
    } else if (!live_dag_peer_queues_.empty() && !live_dag_batch_timer_->active()) {
        live_dag_batch_timer_->schedule_after(std::chrono::milliseconds(LIVE_DAG_BATCH_DELAY_MS));
    }
}

void NetworkService::flush_live_dag_batches() {
    std::vector<std::pair<std::string, DagTransactionBatch>> batches;
    for (auto queue = live_dag_peer_queues_.begin(); queue != live_dag_peer_queues_.end();) {
        auto meta = peer_meta_for(queue->first);
        if (!meta.has_value() || !meta.value().supports_dag_tx_batch()) {
            queue = live_dag_peer_queues_.erase(queue);
            continue;
        }

        DagTransactionBatch batch;
        std::size_t         bytes = 0;
        while (!queue->second.entries.empty() && batch.transactions.size() < LIVE_DAG_BATCH_MAX_TRANSACTIONS) {
            const auto &entry = queue->second.entries.front();
            if (!batch.transactions.empty() && bytes + entry.serialized_size > LIVE_DAG_BATCH_MAX_BYTES - 1024) {
                break;
            }
            bytes += entry.serialized_size;
            queue->second.bytes -= entry.serialized_size;
            batch.transactions.push_back(std::move(queue->second.entries.front().transaction));
            queue->second.entries.pop_front();
        }
        if (!batch.transactions.empty())
            batches.emplace_back(queue->first, std::move(batch));
        if (queue->second.entries.empty()) {
            queue = live_dag_peer_queues_.erase(queue);
        } else {
            ++queue;
        }
    }

    for (auto &[identifier, batch] : batches)
        send_live_dag_batch(identifier, std::move(batch));
    if (!live_dag_peer_queues_.empty())
        live_dag_batch_timer_->schedule_after(std::chrono::milliseconds(LIVE_DAG_BATCH_DELAY_MS));
}

void NetworkService::send_live_dag_batch(const std::string &peer_identifier, DagTransactionBatch batch) {
    std::string serialized;
    {
        WireFormat::Scope scope(WireFormat::Mode::Canonical);
        serialized = MessagePack::serialize(batch);
    }
    if (serialized.empty() || serialized.size() > LIVE_DAG_BATCH_MAX_BYTES)
        return;

    auto &main_actor = node->account_controller()->system_actor();
    auto  message    = make_init_message(serialized,
                                     SendMode::Broadcast,
                                     MessageType::DagTransactionBatch,
                                     MessageStatus::NoStatus,
                                     main_actor.id(),
                                     "",
                                     node->node_identifier());
    add_all_services_identifiers_to_message(message);
    const auto message_hash = message.calculate_hash();
    auto       signature    = main_actor.key().sign(ByteArray(message_hash).toBytes());
    if (!signature.has_value())
        return;
    const auto blob = message.serialize() + ByteArray(signature.value()).toString();
    send_message_connections(blob,
                             blob,
                             message,
                             SendMode::Broadcast,
                             peer_identifier,
                             MessageType::DagTransactionBatch,
                             MessageStatus::NoStatus);
}

void NetworkService::send_live_dag_transaction_to_legacy(const Transaction &transaction) {
    std::string canonical;
    std::string legacy;
    {
        WireFormat::Scope scope(WireFormat::Mode::Canonical);
        canonical = MessagePack::serialize(transaction);
    }
    {
        WireFormat::Scope scope(WireFormat::Mode::Legacy);
        legacy = MessagePack::serialize(transaction);
    }

    auto &main_actor = node->account_controller()->system_actor();
    auto  message    = make_init_message(canonical,
                                     SendMode::Broadcast,
                                     MessageType::DagTransaction,
                                     MessageStatus::NoStatus,
                                     main_actor.id(),
                                     "",
                                     node->node_identifier());
    add_all_services_identifiers_to_message(message);
    auto legacy_message = message;
    legacy_message.data = legacy;
    const auto sign     = [&](MessageBody &body) -> std::optional<std::string> {
        auto signature = main_actor.key().sign(ByteArray(body.calculate_hash()).toBytes());
        if (!signature.has_value())
            return std::nullopt;
        return body.serialize() + ByteArray(signature.value()).toString();
    };
    auto canonical_blob = sign(message);
    auto legacy_blob    = sign(legacy_message);
    if (!canonical_blob.has_value() || !legacy_blob.has_value())
        return;
    send_message_connections(canonical_blob.value(),
                             legacy_blob.value(),
                             message,
                             SendMode::Broadcast,
                             "",
                             MessageType::DagTransaction,
                             MessageStatus::NoStatus,
                             PeerSelection::LegacyDag);
}

void NetworkService::send_broadcast_message_further(const NetworkPackageStorage &package_data,
                                                    bool                         legacy_dag_only) {
    if (package_data.msg_body.send_type != SendMode::Broadcast) {
        eWarning("Send Broadcast Message error - wrong network send type: {}", package_data.msg_body.send_type);
        return;
    }

    auto network_forwarded_messages_locked = *forwarded_messages_;
    if (network_forwarded_messages_locked->contains(package_data.msg_body.message_id)) {
        eWarning("Send Broadcast Message error - message with the same message ID has already been sent: {}",
                 package_data.msg_body.message_id);
        return;
    }

    auto &mainActor = node->account_controller()->system_actor();

    MessageBody message_edited = package_data.msg_body;
    message_edited.sender_id   = node->account_controller()->system_actor().id();
    message_edited.nodes_identifiers_to_ignore.emplace(package_data.prev_identifier);
    add_all_services_identifiers_to_message(message_edited);

    auto serialized = message_edited.serialize();
    auto full_blob  = serialized + package_data.sign;
    send_message_connections(full_blob,
                             full_blob,
                             message_edited,
                             SendMode::Broadcast,
                             "",
                             message_edited.message_type,
                             message_edited.status,
                             legacy_dag_only ? PeerSelection::LegacyDag : PeerSelection::All);

    // eTemp("Message forwarded with messageId: {}", package_data.msg_body.message_id);

    network_forwarded_messages_locked->emplace(message_edited.message_id,
                                               std::make_pair(package_data.prev_identifier,
                                                              std::chrono::steady_clock::now()));
    const auto max_entries = node->runtime_limits().cached_transactions;
    while (network_forwarded_messages_locked->size() > max_entries) {
        network_forwarded_messages_locked->erase(network_forwarded_messages_locked->begin());
    }
    schedule_cache_cleanup();
}

void NetworkService::save_to_cache(const std::string &serialized_message,
                                   SendMode           send_mode,
                                   const std::string &receiver_identifier) {
    static_cast<void>(serialized_message);
    static_cast<void>(send_mode);
    static_cast<void>(receiver_identifier);
}

void NetworkService::send_from_cache() {
    std::error_code error;
    std::filesystem::remove(NetworkCacheFile, error);
    if (error) {
        eWarning("[NetworkService] Cannot remove disabled cache file {}: {}", NetworkCacheFile, error.message());
    }
}

bool NetworkService::is_connection_exists(const std::string &identifier) {
    auto connections_locked = *connections_;
    for (const auto &service : *connections_locked) {
        if (!service->is_active()) {
            continue;
        }
        if (service->identifier() == identifier) {
            return true;
        }
    }

    return false;
}

bool NetworkService::is_active_connection_exists() {
    auto connectionsLocked = *connections_;
    if (connectionsLocked->empty()) {
        return false;
    }

    for (const auto &el : *connectionsLocked) {
        if (el && el->is_active()) {
            return true;
        }
    }

    return false;
}

int NetworkService::active_connections_count() {
    auto connectionsLocked = *connections_;
    if (connectionsLocked->empty()) {
        return 0;
    }

    int count = 0;
    for (const auto &el : *connectionsLocked) {
        if (el && el->is_active()) {
            count++;
        }
    }

    return count;
}

int NetworkService::max_connections() const {
    const auto profile_limit = node->runtime_limits().peer_limit;
    return profile_limit == 0 ? static_cast<int>(Network::maxConnections) : static_cast<int>(profile_limit);
}

std::vector<std::string> NetworkService::active_connection_identifiers() const {
    std::vector<std::string> identifiers;
    auto                     connectionsLocked = *connections();
    identifiers.reserve(connectionsLocked->size());

    for (const auto &service : *connectionsLocked) {
        if (!service || !service->is_active()) {
            continue;
        }

        auto identifier = service->identifier();
        if (!identifier.empty()) {
            identifiers.push_back(std::move(identifier));
        }
    }

    std::sort(identifiers.begin(), identifiers.end());
    identifiers.erase(std::unique(identifiers.begin(), identifiers.end()), identifiers.end());
    return identifiers;
}

std::int64_t NetworkService::connection_pending_bytes(const std::string &identifier) const {
    auto connections_locked = *connections();
    for (const auto &service : *connections_locked) {
        if (service != nullptr && service->is_active() && service->identifier() == identifier) {
            return service->pending_bytes();
        }
    }
    return 0;
}
bool NetworkService::check_message_count(const std::string &msg) {
    bool        flag_result = true;
    std::string hashMsg     = Utils::calculate_hash(msg);
    auto        it          = msg_hash_list_.find(hashMsg);

    if (it == msg_hash_list_.end()) {
        msg_hash_list_.emplace(hashMsg, std::make_pair(0, std::chrono::steady_clock::now()));
        const auto max_entries = node->runtime_limits().cached_transactions;
        while (msg_hash_list_.size() > max_entries) {
            msg_hash_list_.erase(msg_hash_list_.begin());
        }
        schedule_cache_cleanup();
    } else {
        if (connections_->empty() || it->second.first == connections_->size() - 1) {
            msg_hash_list_.erase(it);
            flag_result = false;
        } else {
            it->second.first++;
            flag_result = true;
        }
    }

    return flag_result;
}

void NetworkService::message_received(const std::string &message,
                                      const std::string &ip,
                                      const std::string &identifier) {
    // eLog("node_enabled {}", node_enabled.load());
    if (!node_enabled.load()) {
        return;
    }

    if (!check_message_count(message)) {
        eLog("[Network Manager] checkMsgCount have returned false: such message has been already added");
        return;
    }

    std::string_view msg  = std::string_view(message).substr(0, message.size() - 64);
    std::string_view sign = std::string_view(message).substr(message.size() - 64, 64);

    auto message_body_expected = MessagePack::deserialize<MessageBody>(msg);
    if (!message_body_expected.has_value()) {
        eWarning("[NetworkService] message_received: can't deserialize message body");
        return;
    }

    MessageBody message_body = std::move(message_body_expected).value();
    const auto  node_id =
        NodeId { .actor_id = message_body.init_sender_id, .node_identifier = message_body.init_sender_identifier };

    /*
    auto sign_actor = node->actorIndex()->get_actor(message_body.init_sender_id, ActorGetType::NoRequest);
    if (!sign_actor.has_value()
        && (message_body.message_type == MessageType::NewActor
            || message_body.message_type == MessageType::Actor)) {
        auto actor_result = MessagePack::deserialize<Actor<KeyPublic>>(message_body.data);
        if (!actor_result.has_value()) {
            return;
        }
        sign_actor = actor_result.value();
    }

     if (sign_actor.has_value()) {
         auto verify = sign_actor.value().key().verify(ByteArray(message_body.calculate_hash()).toBytes(),
                                                       ByteArray(sign.data()).toArray<crypto_sign_BYTES>());
         if (!verify.has_value()) {
             eWarning("[Network] Can't verify message");
             return;
         }
         if (!verify) {
             eWarning("[Network] Sign package is invalid!");
             return;
         }

      } else {
          // if (message_body.message_type != MessageType::NewActor) {
          return;
          // }
      }
      */

    const SendMode      send_type    = message_body.send_type;
    const MessageType   type         = message_body.message_type;
    const MessageStatus status       = message_body.status;
    const std::string  &serialized   = message_body.data;
    const std::string  &message_id   = message_body.message_id;
    const bool          is_luminance = node_id.actor_id == node->network_id();

    if (status == MessageStatus::Request || status == MessageStatus::NoStatus) {
        bool should_ignore = (type == MessageType::DagTransaction || type == MessageType::NewActor
                              || type == MessageType::CoinReward);

        {
            auto messages_locked = *messages_;
            if (!should_ignore
                && (messages_locked->contains(message_id)
                    || message_body.init_sender_id == node->account_controller()->system_actor().id())) {
                return;
            }

            auto res =
                messages_locked->emplace(message_id, std::make_pair(identifier, std::chrono::steady_clock::now()));
            if (!res.second) {
                return;
            }

            const auto max_entries = node->runtime_limits().cached_transactions;
            while (messages_locked->size() > max_entries) {
                messages_locked->erase(messages_locked->begin());
            }
        }
        schedule_cache_cleanup();
    } else if (status == MessageStatus::Response) {
        auto network_forwarded_messages_locked = *forwarded_messages_;
        auto searchRes                         = network_forwarded_messages_locked->find(message_id);
        if (searchRes != network_forwarded_messages_locked->end()) {
            MessageBody message_edited = message_body;
            message_edited.sender_id   = node->account_controller()->system_actor().id();
            message_edited.nodes_identifiers_to_ignore.emplace(node->node_identifier());

            auto serialized = message_edited.serialize();
            auto blob       = serialized + std::string(sign);
            send_message_connections(blob, blob, message_edited, SendMode::Focused, searchRes->second.first);
            // eWarning(
            //     "Network Message ignored 3: already achieved such Response with messageId: {} from: {}, type:
            //     {}", messageId, identifier, type);

            return;
        }
    }

    const NetworkPackageStorage package_data(message_body, identifier, std::string(sign));
    bool                        is_node = ip == first_node_;

    Responder responder(this);
    responder.set_message_id(message_id);
    responder.add_identifier(identifier);
    responder.set_message_type(type);
    responder.set_node_id(node_id);
    responder.set_ip(ip);

    int luminance = node->luminance_manager()->read_luminance(node_id);
    responder.set_luminance(luminance == -1 ? 1 : luminance);

    // Network ID (is_luminance) and the configured bootstrap peer (is_node) both
    // get a luminance boost so their responses are preferred during sync.
    if (is_luminance || is_node) {
        responder.set_luminance(responder.luminance() * 10);
    }

#ifndef NDEBUG
    if (Network::networkDebug) {
        msgpack::object_handle oh           = msgpack::unpack(serialized.data(), serialized.size());
        msgpack::object        deserialized = oh.get();
        eLog("[Network Message] Received: type {}, status {}, id {}, body: {}",
             type,
             status,
             message_id,
             (std::stringstream() << deserialized).str());
    }
#endif

    calculate_traffic_->add_bytes_received(ip, message.size());

    // timer.start();

    // TODO: not global
    //
    // Count every non-Custom message, not just broadcasts. Luminance gates sync
    // responses, interval claims and control-range responses at < 2 — but a node
    // that only ANSWERS requests (a passive client, a quiet server) never broadcasts,
    // so its luminance never grew and its peers dropped exactly the messages needed
    // to heal divergence. Measured: a REAL-profile stand (only the server sends
    // transactions) never converged, with the server logging
    // "Sync responce dropped: identifiers=1, luminance=1" for every client answer.
    if (type != MessageType::Custom) {
        node->luminance_manager()->increment(node_id);
    }

    // Inner payloads arrive in the wire format (hex during the legacy-interop
    // transition). Parse every per-type MessagePack::deserialize below under
    // that scope so BigNumber/BigNumberFloat decode correctly regardless of hop.
    // Responses generated inside the switch go through send_message(), which
    // forces its own scope, so leaving this active across the switch is safe.
    WireFormat::Scope wire_scope(WireFormat::wire());

    // Lifecycle gate: drop Dag network traffic (types 30..48) while the Dag is
    // stopped, so handlers don't race against shutdown/migration. Enforced once
    // here at the dispatch layer instead of per-handler.
    {
        auto type_val = std::to_underlying(type);
        if (type_val >= std::to_underlying(MessageType::DagTransaction)
            && type_val <= std::to_underlying(MessageType::DagTransactionBatch)) {
            auto *dag = node->dag();
            if (dag && !dag->is_accepting_messages()) {
                return;
            }
        }
    }

    // try {
    switch (type) {
    case MessageType::Custom: {
        // eSuccess("Achieved Custom package. MessageID: {} | SenderId: {} | Status: {} | Identifier: {}",
        //          messageId,
        //          message_body.sender_id,
        //          magic_enum::enum_name(status),
        //          identifier);

        const auto custom_deserialize_result = MessagePack::deserialize<CustomMessage>(serialized);

        if (!custom_deserialize_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for custom message", type);
            return;
        }

        if (node->is_custom_app_) {
            custom_message_event_.publish(package_data, custom_deserialize_result.value());
        } else {
            send_broadcast_message_further(package_data);
        }

        break;
    }

    case MessageType::ShareConnections: {
        if (status == MessageStatus::Request) {
            eLog("Achieved ShareConnections(Request) {}", message_id);
            std::vector<std::string> available_ips;

            {
                auto locked_connections = *connections_;
                for (const auto &connection : *locked_connections) {
                    if (identifier != connection->identifier()) {
                        if (connection->ip().empty())
                            continue;
                        available_ips.emplace_back(connection->ip());
                    }
                }
            }

            if (!available_ips.empty()) {
                node->network()->send_message(MessagePack::serialize_container(available_ips),
                                              MessageType::ShareConnections,
                                              SendMode::Focused,
                                              MessageStatus::Response,
                                              responder);
            }
        } else if (status == MessageStatus::Response) {
            eLog("Achieved ShareConnections(Response) {}", message_id);
            auto serialized_ips_result = MessagePack::deserialize<std::vector<std::string>>(serialized);
            if (!serialized_ips_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for ips vector in {} state", type, status);
                return;
            }

            auto deserialized_ips_result =
                MessagePack::deserialize_container<std::string>(serialized_ips_result.value());
            if (!deserialized_ips_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for string container in {} state",
                         type,
                         status);
                return;
            }

            for (const auto &ip_address : deserialized_ips_result.value()) {
                bool can_connect        = true;
                auto locked_connections = *connections_;
                for (const auto &existing_connection : *locked_connections) {
                    if (ip_address == existing_connection->ip()) {
                        can_connect = false;
                        break;
                    }
                }

                if (can_connect)
                    request_connection(ip_address);
            }
        }
        break;
    }

    case MessageType::ResponseDfsSize: {
        const auto dfs_size_result = MessagePack::deserialize<DfsP::ResponseDfsSize>(serialized);
        if (!dfs_size_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for response dfs size", type);
            return;
        }

        if (Utils::globalVariableOfDfsSize < dfs_size_result.value().size) {
            Utils::globalVariableOfDfsSize = dfs_size_result.value().size;
        }

        break;
    }

    case MessageType::RequestDfsSize: {
        const auto dfs_request_result = MessagePack::deserialize<DfsP::RequestDfsSize>(serialized);
        if (!dfs_request_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for request dfs size", type);
            return;
        }

        node->dfs_service()->sendSizeReponseMsg(dfs_request_result.value(), responder);
        break;
    }

    case MessageType::NewActor: {
        auto new_actor_result = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
        if (!new_actor_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for new actor", type);
            return;
        }

        auto actor_handling_result = node->actor_index()->network_store_new_actor(new_actor_result.value());
        if (actor_handling_result.has_value()) {
            send_broadcast_message_further(package_data);
        }
        break;
    }

    case MessageType::Actor: {
        break;
        if (status == MessageStatus::Request) {
            auto actor_id_result = MessagePack::deserialize<ActorId>(serialized);
            if (!actor_id_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for ActorId in {} state", type, status);
                break;
            }

            node->actor_index()->network_actor_request(actor_id_result.value(), responder);
        } else if (status == MessageStatus::Response) {
            auto actor_result = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
            if (!actor_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for Actor in {} state", type, status);
                break;
            }

            auto save_result = node->actor_index()->save_actor(actor_result.value());
            if (!save_result.has_value() && save_result.error() != ActorSaveError::AlreadyExists) {
                eWarning("[NetworkService] Cannot save actor: error {}", static_cast<int>(save_result.error()));
            }
        }

        break;
    }

    case MessageType::Actors: {
        if (status == MessageStatus::Request) {
            auto ignored_actor_id_result = MessagePack::deserialize<std::set<ActorId>>(serialized);
            if (!ignored_actor_id_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for ignored ActorId in {} state",
                         type,
                         status);
                break;
            }

            // node->actorIndex()->network_actors_request(ignored_actor_id_result.value(), responder);
        } else if (status == MessageStatus::Response) {
            auto actors_list_result = MessagePack::deserialize<std::vector<Actor<KeyPublic>>>(serialized);
            if (!actors_list_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for actors vector in {} state", type, status);
                break;
            }

            node->actor_index()->network_actors_response(actors_list_result.value());
        }
        break;
    }

    case MessageType::ActorsHash: {
        if (status == MessageStatus::Request) {
            auto actors = MessagePack::deserialize<std::pair<std::uint64_t, std::vector<uint8_t>>>(serialized);
            if (!actors.has_value()) {
                eWarning("[NetworkService] {} deserialization failed in {} state", type, status);
                break;
            }

            node->actor_index()->network_actors_hash_request(actors->first, actors->second, responder);
        } else if (status == MessageStatus::Response) {
            auto actors_list_result = MessagePack::deserialize<std::vector<Actor<KeyPublic>>>(serialized);
            if (!actors_list_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed {} state", type, status);
                break;
            }

            node->actor_index()->network_actors_response(actors_list_result.value());
        }
        break;
    }

        // case MessageType::DfsDirData: {
        //     if (status == MessageStatus::Request) {
        //         auto dir_actor_id_result = MessagePack::deserialize<ActorId>(serialized);
        //         if (!dir_actor_id_result.has_value()) {
        //             eWarning("[NetworkService] {} deserialization failed for ActorId in {} state", type,
        //             status); break;
        //         }
        //     } else if (status == MessageStatus::Response) {
        //         auto dir_data_result =
        //             MessagePack::deserialize<std::pair<ActorId, std::vector<Dfs::DirRow>>>(serialized);
        //         if (!dir_data_result.has_value()) {
        //             eWarning("[NetworkService] {} deserialization failed for directory data in {} state",
        //                      type,
        //                      status);
        //             break;
        //         }
        //         const auto &[owner_id, dir_rows] = dir_data_result.value();
        //     }
        //     break;
        // }

    case MessageType::DfsSyncDirs: {
        if (status == MessageStatus::Request) {
            node->dfs_service()->dirs_manager().network_request_sync(responder);
        } else if (status == MessageStatus::Response) {
            auto last_modified_result = MessagePack::deserialize<std::uint64_t>(serialized);

            if (!last_modified_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for last modified", type);
                break;
            }

            node->dfs_service()->dirs_manager().network_response_sync(last_modified_result.value(), responder);
        }

        break;
    }

    case MessageType::DfsSyncDirsRows: {
        auto dirs_rows_result =
            MessagePack::deserialize<std::vector<Dfs::Tables::DirsFile::DirsSpace::DirsRow>>(serialized);
        if (!dirs_rows_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for dirs rows", type);
            break;
        }

        node->dfs_service()->dirs_manager().network_response_from_last_modified(dirs_rows_result.value(),
                                                                                responder);

        break;
    }

    case MessageType::DfsSyncDirRows: {
        if (status == MessageStatus::Request) {
            auto dirs_row_result = MessagePack::deserialize<Dfs::Tables::DirsFile::DirsSpace::DirsRow>(serialized);
            if (!dirs_row_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for dirs row", type);
                return;
            }

            node->dfs_service()->dirs_manager().network_request_dir_rows(dirs_row_result.value(), responder);
        } else if (status == MessageStatus::Response) {
            auto dirs_row_result =
                MessagePack::deserialize<std::vector<std::pair<ActorId, std::vector<Dfs::DirRow>>>>(serialized);
            if (!dirs_row_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for dir rows", type);
                return;
            }

            node->dfs_service()->dirs_manager().network_response_dir_rows(dirs_row_result.value(), responder);
        }
        break;
    }

    case MessageType::DfsTempSyncAll: {
        auto res = MessagePack::deserialize<bool>(serialized);
        if (res.has_value()) {
            node->dfs_service()->dirs_manager().network_request_all(responder);
            break;
        }

        auto actors_result = MessagePack::deserialize<std::vector<ActorId>>(serialized);
        if (!actors_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for startup DFS sync request", type);
            break;
        }

        node->dfs_service()->dirs_manager().network_request_all(responder, actors_result.value());
        break;
    }

    case MessageType::DfsStoreFile: {
        auto file_link_result = MessagePack::deserialize<Dfs::FileData>(serialized);
        if (!file_link_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for DirRow", type);
            return;
        }

        file_link_result->dir_row.state = Dfs::FileState::Known;
        node->dfs_service()->network_store_file(file_link_result->owner_id,
                                                file_link_result->dir_row,
                                                Dfs::NetworkStoreFile::Broadcast);
        send_broadcast_message_further(package_data);

        break;
    }

    case MessageType::DfsFileExistNotification: {
        auto file_state_result = MessagePack::deserialize<Dfs::Packets::FileState>(serialized);
        if (!file_state_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for file state", type);
            break;
        }

        node->dfs_service()->network_response_file_state(file_state_result.value(), responder);
        break;
    }
    case MessageType::DfsFileFragment: {
        // Bulk payload off the dispatch thread: during a replication wave the
        // MB-sized deserialize + disk write queued for seconds ahead of consensus
        // messages and delayed transactions fell out of the accept window
        // (TooSectionDiff). Per-file striped locks serialize disk writes, and
        // SafePtr guards bookkeeping, so pool execution is safe.
        node->post_storage([this, serialized = std::string(serialized), identifier]() {
            auto fragment_data_result = MessagePack::deserialize<Dfs::Packets::FragmentData>(serialized);
            if (!fragment_data_result.has_value()) {
                eWarning("[NetworkService] DfsFileFragment deserialization failed");
                return;
            }
            node->dfs_service()->download_manager().file_fragment_achieved(fragment_data_result.value(),
                                                                           identifier);
        });

        break;
    }

    case MessageType::DfsFileState: {
        if (status == MessageStatus::Request) {
            auto link_result = MessagePack::deserialize<Dfs::FileLink>(serialized);
            if (!link_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for request file state", type);
                return;
            }

            node->dfs_service()->network_request_file_state(link_result->owner_id,
                                                            link_result->file_id,
                                                            responder);
        } else if (status == MessageStatus::Response) {
            auto file_state_result = MessagePack::deserialize<Dfs::Packets::FileState>(serialized);
            if (!file_state_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for response file state", type);
                return;
            }

            node->dfs_service()->network_response_file_state(file_state_result.value(), responder);
        }
        break;
    }

    case MessageType::DfsFileRequest: {
        auto link_result = MessagePack::deserialize<Dfs::FileLinkFragment>(serialized);
        if (!link_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for file request", type);
            return;
        }

        node->dfs_service()->download_manager().share_stored_file(link_result.value(), responder);

        break;
    }

    case MessageType::DfsFileRequestContinueUpload: {
        auto link_result = MessagePack::deserialize<Dfs::FileLink>(serialized);
        if (!link_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for request file state", type);
            return;
        }

        if (status == MessageStatus::Request)
            node->dfs_service()->network_request_file_existance(link_result.value(), responder);
        else if (status == MessageStatus::Response)
            node->dfs_service()->download_manager().add_node_identifier(link_result.value(), identifier);

        break;
    }

    case MessageType::DfsFileRemove: {
        auto file_remove = MessagePack::deserialize<Dfs::Packets::RemoveFile>(serialized);
        if (!file_remove.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for file remove", type);
            return;
        }

        node->dfs_service()->network_remove_stored_file(file_remove->owner_id,
                                                        file_remove->file_id,
                                                        file_remove->sign,
                                                        file_remove->last_modified);
        // if sign not verify only -> not broadrcast
        send_broadcast_message_further(package_data);
        break;
    }

    case MessageType::DfsCollectionRequest: {
        auto db_request_result = MessagePack::deserialize<std::pair<ActorId, std::string>>(serialized);
        if (!db_request_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for collection request", type);
            return;
        }
        const auto &[actor_id, file_id] = db_request_result.value();
        node->dfs_service()->network_request_collection(actor_id, file_id, responder);

        break;
    }

    case MessageType::DfsCollectionHistory: {
        auto db_history_result =
            MessagePack::deserialize<std::tuple<ActorId, std::string, std::vector<HistoricalCollectionRow>>>(
                serialized);
        if (!db_history_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for collection history", type);
            return;
        }
        const auto &[actor_id, file_id, historical_rows] = db_history_result.value();
        node->dfs_service()->network_response_historical_collection(actor_id, file_id, historical_rows);
        break;
    }

    case MessageType::DfsCollectionContent: {
        auto db_content_result =
            MessagePack::deserialize<std::tuple<ActorId, std::string, std::vector<DbRow>>>(serialized);
        if (!db_content_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for collection content", type);
            return;
        }
        const auto &[actor_id, file_id, db_rows] = db_content_result.value();
        node->dfs_service()->network_response_content_collection(actor_id, file_id, db_rows);
        break;
    }

    case MessageType::DfsCollectionRowChange: {
        auto db_add_result =
            MessagePack::deserialize<std::tuple<ActorId, std::string, HistoricalCollectionRow>>(serialized);
        if (!db_add_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for collection change", type);
            return;
        }
        const auto &[actor_id, file_id, historical_row] = db_add_result.value();
        node->dfs_service()->network_change_collection(actor_id, file_id, historical_row, responder);
        break;
    }

    case MessageType::DfsVectorCreation:
    case MessageType::DfsVectorContent: {
        auto db_content_result = MessagePack::deserialize<Dfs::Packets::DfsVectorContentPackage>(serialized);
        if (!db_content_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for vector content", type);
            return;
        }

        node->dfs_service()->network_response_content_vector(db_content_result.value());

        if (type == MessageType::DfsVectorCreation) {
            send_broadcast_message_further(package_data);
        }
        break;
    }

    case MessageType::DfsVectorAdd: {
        auto db_content_result = MessagePack::deserialize<Dfs::Packets::VectorRowAdd>(serialized);
        if (!db_content_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for vector add", type);
            return;
        }

        node->dfs_service()->network_vector_add(db_content_result->owner_id,
                                                db_content_result->file_id,
                                                db_content_result->row);

        send_broadcast_message_further(package_data);
        break;
    }

        /*
           case MessageType::DfsVerifyList: {
               switch (status) {
               case MessageStatus::NoStatus:
                   break;
               case MessageStatus::Request: {
                   auto serialized_messages_result =
           MessagePack::deserialize<std::vector<std::string>>(serialized); if
           (!serialized_messages_result.has_value()) { eWarning("[NetworkService] {} deserialization failed for
           list of serialized messages in {} state", type, status); break;
                   }
                   auto verify_files_result =
                       MessagePack::deserialize_container<DfsP::VerifyFileMessage>(serialized_messages_result.value());
                   if (!verify_files_result.has_value()) {
                       eWarning("[NetworkService] {} deserialization failed for list of verify messages in {}
           state", type, status); break;
                   }
                   node->dfs_service()->verifyFiles(verify_files_result.value(), messageId);
                   break;
               }

                 case MessageStatus::Response: {
                     auto serialized_messages_result =
             MessagePack::deserialize<std::vector<std::string>>(serialized); if
             (!serialized_messages_result.has_value()) { eWarning("[NetworkService] {} deserialization failed for
             list of serialized messages in {} state", type, status); break;
                     }
                     auto verify_files_result =
                         MessagePack::deserialize_container<DfsP::VerifyFileMessage>(serialized_messages_result.value());
                     if (!verify_files_result.has_value()) {
                         eWarning("[NetworkService] {} deserialization failed for list of verify messages in {}
             state", type, status); break;
                     }
                     float verify_percent = node->dfs_service()->percentVerified(verify_files_result.value());
                     break;
                 }
                 }
                 break;
             }

                 case MessageStatus::Response: {
                     auto serialized_messages_result =
             MessagePack::deserialize<std::vector<std::string>>(serialized); if
             (!serialized_messages_result.has_value()) { eWarning("[NetworkService] {} deserialization failed for
             list of serialized messages in {} state", type, status); break;
                     }
                     auto verify_files_result =
                         MessagePack::deserialize_container<DfsP::VerifyFileMessage>(serialized_messages_result.value());
                     if (!verify_files_result.has_value()) {
                         eWarning("[NetworkService] {} deserialization failed for list of verify messages in {}
             state", type, status); break;
                     }
                     float verify_percent = node->dfs_service()->percentVerified(verify_files_result.value());
                     break;
                 }
                 }
                 break;
             }
         */

    case MessageType::DagTransaction: {
        if (!node->dag()->should_queue_network_transaction()) {
            break;
        }
        auto transaction_result = MessagePack::deserialize<Transaction>(serialized);
        if (!transaction_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for transaction", type);
            break;
        }

        const auto transaction         = transaction_result.value();
        auto       ignored_identifiers = package_data.msg_body.nodes_identifiers_to_ignore;
        ignored_identifiers.insert(package_data.msg_body.nodes_identifiers_to_ignore_later.begin(),
                                   package_data.msg_body.nodes_identifiers_to_ignore_later.end());
        ignored_identifiers.insert(package_data.prev_identifier);
        node->dag()->submit_network_transaction(transaction,
                                                responder,
                                                [this,
                                                 transaction,
                                                 identifier,
                                                 ignored_identifiers,
                                                 package_data](std::expected<void, TransactionProveError> result,
                                                               bool should_forward) {
                                                    // A committed transaction is forwarded once. Invalid traffic,
                                                    // queue overflow, and idempotent replays stop at this node.
                                                    if (result.has_value() && should_forward) {
                                                        remember_live_dag_hash(transaction.hash());
                                                        queue_live_dag_transaction(transaction,
                                                                                   identifier,
                                                                                   ignored_identifiers);
                                                        send_broadcast_message_further(package_data, true);
                                                    }
                                                });
        break;
    }

    case MessageType::DagTransactionBatch: {
        if (!node->dag()->should_queue_network_transaction() || serialized.size() > LIVE_DAG_BATCH_MAX_BYTES) {
            break;
        }
        const auto meta = peer_meta_for(identifier);
        if (!meta.has_value() || !meta.value().supports_dag_tx_batch()) {
            eWarning("[NetworkService] DAG batch from a peer without negotiated support");
            break;
        }
        // The batch path is negotiated between >= 0.26 peers only and is serialized
        // in Canonical (see send_live_dag_batch) — unlike the legacy-interop wire,
        // which is hex. Without this scope the dispatch-level Legacy scope applies
        // and every decimal number is read as hex: a transaction stamped for section
        // 25039 arrives as 151609 (0x25039), is rejected as TooSectionDiff forever,
        // and the re-check loop grinds on it indefinitely.
        WireFormat::Scope batch_scope(WireFormat::Mode::Canonical);
        auto batch_result = MessagePack::deserialize<DagTransactionBatch>(serialized);
        if (!batch_result.has_value() || batch_result.value().transactions.empty()
            || batch_result.value().transactions.size() > LIVE_DAG_BATCH_MAX_TRANSACTIONS) {
            eWarning("[NetworkService] Invalid DAG transaction batch");
            break;
        }

        auto ignored_identifiers = package_data.msg_body.nodes_identifiers_to_ignore;
        ignored_identifiers.insert(package_data.msg_body.nodes_identifiers_to_ignore_later.begin(),
                                   package_data.msg_body.nodes_identifiers_to_ignore_later.end());
        ignored_identifiers.insert(package_data.prev_identifier);
        for (const auto &transaction : batch_result.value().transactions) {
            if (is_contract_transaction(transaction.type()) || transaction.type() == TransactionType::Genesis
                || transaction.type() == TransactionType::Balance) {
                continue;
            }
            const auto hash = transaction.hash();
            if (hash.empty() || !remember_live_dag_hash(hash))
                continue;

            Responder batch_responder;
            batch_responder.set_ip(identifier);
            node->dag()->submit_network_transaction(transaction,
                                                    batch_responder,
                                                    [this,
                                                     transaction,
                                                     identifier,
                                                     ignored_identifiers,
                                                     hash](std::expected<void, TransactionProveError> result,
                                                           bool should_forward) {
                                                        if (!result.has_value() || !should_forward) {
                                                            forget_live_dag_hash(hash);
                                                            return;
                                                        }
                                                        queue_live_dag_transaction(transaction,
                                                                                   identifier,
                                                                                   ignored_identifiers);
                                                        send_live_dag_transaction_to_legacy(transaction);
                                                    });
        }
        break;
    }

    case MessageType::DagTransactionResult: {
#ifdef IS_APP_UI_CLIENT // only for ui clients, not for consoles, luminance priority
        if (!is_luminance) {
            return;
        }
#endif

        auto transaction_result = MessagePack::deserialize<TransactionResult>(serialized);
        if (!transaction_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for transaction result", type);
            break;
        }

        node->dag()->network_transaction_result(transaction_result.value(), responder);
        break;
    }

    case MessageType::DagSections: {
        if (status == MessageStatus::Request) {
            auto range = MessagePack::deserialize<SectionRange>(serialized);
            if (!range.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for dag sync vector", type);
                break;
            }

            auto first = BigNumber::create(range->first);
            auto last  = BigNumber::create(range->last);
            if (!first.has_value() || !last.has_value()) {
                break;
            }

            node->dag()->network_request_sections(first.value(), last.value(), responder);
        }
        break;
    }

    case MessageType::DagFileSections: {
        if (status == MessageStatus::Request) {
            auto range = MessagePack::deserialize<SectionRange>(serialized);
            if (!range.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for file sections request", type);
                break;
            }

            // SectionRange ids are wire-format strings (hex during the legacy
            // transition), matching how request_file_sections encodes them.
            bool wire_hex = WireFormat::wire() == WireFormat::Mode::Legacy;
            if (wire_hex) {
                node->dag()->network_request_file_sections(BigNumber::from_hex(range->first),
                                                           BigNumber::from_hex(range->last),
                                                           responder);
            } else {
                auto first = BigNumber::create(range->first);
                auto last  = BigNumber::create(range->last);
                if (!first.has_value() || !last.has_value()) {
                    break;
                }
                node->dag()->network_request_file_sections(first.value(), last.value(), responder);
            }
        } else if (status == MessageStatus::Response) {
            auto data = MessagePack::deserialize<std::string>(serialized);
            if (!data.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for file sections response", type);
                break;
            }

            node->dag()->network_file_sections_response(data.value(), responder);
        }

        break;
    }

    case MessageType::DagPackList: {
        if (status == MessageStatus::Request) {
            node->dag()->network_pack_list_request(responder);
        } else if (status == MessageStatus::Response) {
            auto list = MessagePack::deserialize<PackList>(serialized);
            if (!list.has_value()) {
                eWarning("[NetworkService] {} deserialization failed", type);
                break;
            }
            node->dag()->network_pack_list_response(list.value(), responder);
        }
        break;
    }

    case MessageType::DagPackRequest: {
        if (status == MessageStatus::Request) {
            auto req = MessagePack::deserialize<PackRequest>(serialized);
            if (!req.has_value()) {
                eWarning("[NetworkService] {} deserialization failed", type);
                break;
            }
            node->dag()->network_pack_request(req.value(), responder);
        }
        break;
    }

    case MessageType::DagPackData: {
        if (status == MessageStatus::Response) {
            auto data = MessagePack::deserialize<PackData>(serialized);
            if (!data.has_value()) {
                eWarning("[NetworkService] {} deserialization failed", type);
                break;
            }
            node->dag()->network_pack_data_response(data.value(), responder);
        }
        break;
    }

    case MessageType::DagCacheSnapshotRequest: {
        if (status == MessageStatus::Request) {
            node->dag()->network_cache_snapshot_request(responder);
        }
        break;
    }

    case MessageType::DagCacheSnapshotData: {
        if (status == MessageStatus::Response) {
            auto data = MessagePack::deserialize<std::string>(serialized);
            if (!data.has_value()) {
                eWarning("[NetworkService] {} deserialization failed", type);
                break;
            }
            node->dag()->network_cache_snapshot_response(data.value(), responder);
        }
        break;
    }

    case MessageType::DagLightData: {
        if (status == MessageStatus::Request) {
#ifdef IS_APP_UI_CLIENT // only for ui clients, not for consoles, luminance priority
            if (!is_luminance) {
                return;
            }
#endif

            auto range = MessagePack::deserialize<bool>(serialized);
            if (!range.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for dag sync vector", type);
                break;
            }

            node->dag()->network_request_light(responder);
        } else if (status == MessageStatus::Response) {
            auto light = MessagePack::deserialize<DagLightPackage>(serialized);
            if (!light.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for dag sync light", type);
                break;
            }

            node->dag()->network_response_light(light.value(), responder);
        }
        break;
    }

    case MessageType::CoinReward: {
        auto reward_request_result = MessagePack::deserialize<Dfs::Reward::RequestReward>(serialized);
        if (!reward_request_result.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for coin reward", type);
            break;
        }
        const auto &reward_request = reward_request_result.value();
        switch (status) {
        case MessageStatus::Request: {
            auto res = node->data_mining_manager()->network_request_coin_reward(reward_request, responder);

            if (res) {
                send_broadcast_message_further(package_data);
            }
            break;
        }
        default:
            break;
        }
        break;
    }

    case MessageType::DagSyncLastInfo: {
#ifdef IS_APP_UI_CLIENT // only for ui clients, not for consoles, luminance priority
        if (!is_luminance && !is_node) {
            return;
        }
#endif

        if (status == MessageStatus::Request) {
            auto last_info_result = MessagePack::deserialize<bool>(serialized);
            if (!last_info_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for dag sync vector", type);
                break;
            }

            node->dag()->network_status_sync_request(responder);
        } else if (status == MessageStatus::Response) {
            auto last_info_result = MessagePack::deserialize<DagLastInfo>(serialized);
            if (!last_info_result.has_value()) {
                eWarning("[NetworkService] {} deserialization failed for dag sync vector", type);
                break;
            }

            node->dag()->network_status_sync_response(last_info_result.value(), responder);
        }
        break;
    }

    case MessageType::DagIntervalHash: {
        auto hash_interval = MessagePack::deserialize<HashInterval>(serialized);
        if (!hash_interval.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for hash interval", type);
            break;
        }

        node->dag()->network_hash_interval(hash_interval.value(), responder);
        break;
    }

    case MessageType::DagControlRangeRequest: {
#ifdef IS_APP_CLIENT // only for not app clients
        return;
#endif

        auto dag_control = MessagePack::deserialize<DagControlRangeRequest>(serialized);
        if (!dag_control.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for dag control", type);
            break;
        }

        node->dag()->network_request_control_section(dag_control.value(), responder);
        break;
    }

    case MessageType::DagControlRangeResponse: {
#ifdef IS_APP_CLIENT // only for ui clients
        if (!is_luminance) {
            return;
        }
#endif

        auto dag_control = MessagePack::deserialize<DagControlRangeResponse>(serialized);
        if (!dag_control.has_value()) {
            eWarning("[NetworkService] {} deserialization failed for dag control", type);
            break;
        }

        node->dag()->network_control_range_response(dag_control.value(), responder);
        break;
    }

    default: {
        eCritical("[NetworkService/messageReceived] Not supported message type: {} ({})",
                  type,
                  std::to_underlying(type));
        break;
    }
    }

    // eLog("Timer: {} ms for {}", timer.elapsed(), type);
}

void NetworkService::remove_socket_connection(SocketService::Ptr connection) {
    if (!connection) {
        return;
    }

    {
        auto connections_locked = connections();
        connections_locked->erase(connection);
        eLog("[WS] Removed {}", fmt::ptr(connection.get()));
    }
    //    m_reconnections.remove(NetworkReconnect {
    //        .ip = connection->ip(), .port = connection->port(), .protocol = Network::Protocol::WebSocket
    //        });
    check_connections_status();
    schedule_reconnection(1000);
}

void NetworkService::socket_error(Network::SocketServiceError error,
                                  std::string                 error_data,
                                  std::string                 ip,
                                  std::string                 identifier,
                                  SocketDirection             direction) {
    eLog("[NetworkService] Error socket: {} {} {} {}", direction, error, ip, identifier);

    // Our own identifier answering an outgoing dial to first_node means first_node is
    // this very node. is_own_address() deliberately excludes loopback (a single-host
    // mesh shares 127.0.0.x), so this is the only place the loop is detectable — and
    // without remembering it, the node redialed itself every second, churning sockets
    // hard enough to drop in-flight responses to real peers.
    if (error == Network::SocketServiceError::IncompatibleIdentifier && direction == SocketDirection::Outgoing
        && ip == first_node_ && identifier == node->node_identifier()) {
        if (!first_node_self_detected_.exchange(true)) {
            eWarning("[Network] first_node {} is this node itself — stopping first-node redials", ip);
        }
        return;
    }

    if (error == Network::SocketServiceError::IncompatibleNetwork
        || error == Network::SocketServiceError::VersionTooOld
        || error == Network::SocketServiceError::VersionTooNew
        || error == Network::SocketServiceError::PeerUnavailable) {
        reconn_.erase(ip);
        if (!Utils::vector_contains(first_nodes_, ip) && ip != first_node_) {
            failed_ips_.insert(ip);
        }
        connection_error_event_.publish(error, ip, identifier, error_data);
        return;
    }

    schedule_reconnection(5000);
}

void NetworkService::local_inizialization() {
    eLog("Doesn't find service. Start find local service");
    using NetworkStatus        = ExtraChain::Core::NetworkStatus;
    network_status_connection_ = network_status_.subscribe([this](NetworkStatus::Status status) {
        switch (status) {
        case NetworkStatus::Status::Online:
            eInfo("World network is online");
            break;
        case NetworkStatus::Status::Offline: {
            eInfo("Warning: World network is offline");
            std::set<SocketService::Ptr> copied;
            {
                auto connectionsLocked = *connections_;
                copied                 = **connections_;
            }

            for (const auto &connection : copied) {
                connection->flush();
                connection->close_connection();
            }
            break;
        }
        case NetworkStatus::Status::Local:
            eInfo("Warning: Local network only");
            break;
        default:
            break;
        }
    });

    const auto address = ExtraChain::Core::NetworkRuntime::local_address();
    if (!address.has_value()) {
        eWarning("[NetworkService] Local address is unavailable: {}", address.error());
        return;
    }
    local_ip_ = address.value();
    eLog("[NetworkService] Found local IP: {}", local_ip_);
}

const std::string &NetworkService::local_ip_value() const noexcept {
    return local_ip_;
}

void NetworkService::initialize_first_node() {
    auto settings = Utils::read_settings();

    if (settings.first_node.has_value()) {
        std::string address = settings.first_node.value();

        if (Utils::is_valid_ip(address) || Utils::is_valid_domain(address)) {
            first_node_ = address;
            return;
        }
    }

    // Version compatibility: 0.17.1
    if (!settings.first_node.has_value()) {
        try {
            std::ifstream first_node_file(".first_node");
            if (first_node_file.is_open()) {
                std::string address;
                std::getline(first_node_file, address);
                first_node_file.close();

                if (Utils::is_valid_ip(address) || Utils::is_valid_domain(address)) {
                    first_node_ = address;
                    save_first_node(first_node_);
                }

                std::error_code error;
                std::filesystem::remove(".first_node", error);
                if (error) {
                    eWarning("[Network] Cannot remove legacy first-node file: {}", error.message());
                }
                return;
            }
        } catch (const std::exception &) {
        }
    }

    save_first_node(first_node_);
}

std::string NetworkService::first_node() {
    return first_node_;
}

bool NetworkService::save_first_node(const std::string_view first_node) {
    if (!Utils::is_valid_ip(first_node) && !Utils::is_valid_domain(first_node)) {
        eWarning("[Network] Incorrect first node: {}", first_node);
        return false;
    }

    first_node_ = first_node;
    first_node_self_detected_.store(false);

    auto settings       = Utils::read_settings();
    settings.first_node = first_node_;
    bool res            = Utils::write_settings(settings);

    if (!res) {
        eWarning("[Network] First node settings write error");
        return false;
    }

    return true;
}

bool NetworkService::remove_one_connection() {
    auto connectionsLocked = *connections_;
    bool isChanged         = false;

    SocketService::Ptr doomed;

    for (auto socket : *connectionsLocked) {
        if (!socket->is_constant()) {
            eLog("[NetworkService] Socket with ip {} was changed to another", socket->ip());

            doomed = socket;
            //
            // connectionsLocked->erase(it);

            // NetworkReconnect tempConnection { .ip       = (*it)->ip(),
            //                                   .port     = (*it)->port(),
            //                                   .protocol = Network::Protocol::WebSocket };

            // auto reconnectionsToIdentifierLocked = *m_reconnectionsToIdentifier;
            // auto findRes                         = reconnectionsToIdentifierLocked->find(tempConnection);
            // if (findRes != reconnectionsToIdentifierLocked->end())
            //     reconnectionsToIdentifierLocked->erase(tempConnection);

            isChanged = true;
            break;
        }
    }

    if (isChanged) {
        doomed->close_connection();
    }

    return isChanged;
}

std::pair<std::string, std::string> NetworkService::public_ip_and_country(std::string ip, bool alt) {
    (void)alt;
    refresh_public_ip_and_country(ip);
    const auto &current = node->public_ip_and_country_value();
    if (!current.first.empty()) {
        return current;
    }
    return { ip.empty() ? public_ip_ : std::move(ip), "Security" };
}

void NetworkService::refresh_public_ip_and_country(std::string ip) {
    const auto target = ip.empty() ? std::string("/json") : "/json/" + ip;
    network_runtime_
        ->async_http_get("ip-api.com",
                         80,
                         target,
                         std::chrono::seconds(5),
                         [this, requested_ip = std::move(ip)](
                             ExtraChain::Core::NetworkRuntime::HttpResult result) mutable {
                             if (!result.has_value()) {
                                 eWarning("[NetworkService] Public IP lookup failed: {}", result.error());
                                 return;
                             }

                             boost::system::error_code parse_error;
                             auto                      document = boost::json::parse(result.value(), parse_error);
                             if (parse_error || !document.is_object()) {
                                 eWarning("[NetworkService] Public IP response is invalid: {}",
                                          parse_error.message());
                                 return;
                             }
                             const auto &object        = document.as_object();
                             const auto *ip_value      = object.if_contains("query");
                             const auto *country_value = object.if_contains("country");
                             if (ip_value == nullptr || country_value == nullptr || !ip_value->is_string()
                                 || !country_value->is_string()) {
                                 eWarning("[NetworkService] Public IP response has no required fields");
                                 return;
                             }

                             std::string resolved_ip =
                                 requested_ip.empty() ? std::string(ip_value->as_string()) : requested_ip;
                             std::string country(country_value->as_string());
                             dispatch_serial(
                                 [this, resolved_ip = std::move(resolved_ip), country = std::move(country)] {
                                     node->set_public_ip_and_country(std::move(resolved_ip), std::move(country));
                                 });
                         });
}

NetworkPackageStorage::NetworkPackageStorage(const MessageBody &body,
                                             const std::string &identifier,
                                             const std::string &signature)
    : msg_body(body)
    , prev_identifier(identifier)
    , sign(signature) {
}
