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

#include <QTimer>
#include <QtCore/QMutex>
#include <QtCore/QRandomGenerator>
#include <QtNetwork/QNetworkAddressEntry>
#include <QtNetwork/QNetworkInterface>
#include <QtNetwork/QNetworkProxy>
#include <string>
#include <shared_mutex>
#include <thread>
#include <memory>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>

#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "network/message_body.h"
#include "network/network_status.h"
#include "network/isocket_service.h"
#include "dfs/dfs_utils.h"
#include "utils/exc_utils.h"

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

class WebSocketService;
class UPNPConnection;
class UPnPConnector;

template<typename T>
using Task = asio::awaitable<T>;
using VoidTask = Task<void>;

class CalculateTraffic {
private:
    struct TrafficStats {
        std::uint64_t bytesSent     = 0;
        std::uint64_t bytesReceived = 0;
    };

    std::unordered_map<std::string, TrafficStats>
                      m_trafficStats; // Container for storing traffic of each connection
    std::shared_mutex m_mutex;        // Mutex for thread safety in Singleton instance access

    // Private constructor to prevent instantiation
    CalculateTraffic() {
    }

    static CalculateTraffic* calculateTraffic_;

public:
    // Deleted copy constructor and assignment operator to prevent copying
    CalculateTraffic(const CalculateTraffic&)            = delete;
    CalculateTraffic& operator=(const CalculateTraffic&) = delete;

    // Static method to access the Singleton instance
    static CalculateTraffic* get_instance();

    // Method for adding sent bytes data for a specific connection
    void add_bytes_sent(const std::string& ip, qint64 bytes);

    // Method for adding received bytes data for a specific connection
    void add_bytes_received(const std::string& connectionId, qint64 bytes);

    // Method for getting the total number of sent bytes data for a specific connection
    qint64 total_bytes_sent_from_connection(const std::string& ip);

    // Method for getting the total number of received bytes data for a specific connection
    qint64 total_bytes_received_from_connection(const std::string& ip);

    // Method for gettint pair of sent and recieved bytes from all connections
    std::pair<uint64_t, uint64_t> total_bytes();
};

struct NetworkReconnect {
    QString           ip;
    quint16           port;
    Network::Protocol protocol;
    // quint64 lastTry;

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
        return NetworkReconnect { .ip       = QString::fromStdString(wsConnection.address),
                                  .port     = static_cast<quint16>(wsConnection.port),
                                  .protocol = Network::Protocol::WebSocket };
    }

    void print() const {
        eLog("[NetworkReconnect] ip: {}, port: {}", ip, port);
    }
};

inline size_t qHash(const NetworkReconnect& reconnect) {
    return qHash(reconnect.ip) + qHash(reconnect.port) + qHash(int(reconnect.protocol));
}

struct MessageIdDataWaiting {
    std::string identifier;
    qint64      time;
    std::string cached_message;
    // msg type
};

struct MessageIdDataReceived {
    std::string identifier;
    qint64      time;
};

static const std::string NetworkCacheFile = "tmp/network.cache";

class Responder {
public:
    Responder(NetworkManager* manager = nullptr)
        : network_manager(manager) {
    }

    Responder(const Responder&) = default;

    template <class T>
    std::string send_response(const T& data, MessageType type, SendMode send_mode, MessageStatus status) const {
        if (network_manager == nullptr) {
            return "";
        }

        auto data_serialized = MessagePack::serialize(data);
        auto send_result     = send_response_impl(data_serialized, type, send_mode, status);
        return send_result;
    }

    const std::string& message_id() const {
        return message_id_;
    }

    const std::string& ip() const {
        return ip_;
    }

    const std::unordered_set<std::string>& identifiers() const {
        return identifiers_;
    }

    const NodeId& node_id() const {
        return node_id_;
    }

    int luminance() const {
        return luminance_;
    }

    bool add_identifier(const std::string& identifier) {
        if (identifier.empty()) {
            return false;
        }

        return identifiers_.insert(identifier).second;
    }

    bool remove_identifier(const std::string& identifier) {
        if (identifier.empty()) {
            return false;
        }

        return identifiers_.erase(identifier) != 0;
    }

    void set_ip(const std::string& ip) {
        ip_ = ip;
    }

    void set_message_id(const std::string& message_id) {
        message_id_ = message_id;
    }

    void set_message_type(MessageType type) {
        message_type_ = type;
    }

    void set_node_id(const NodeId& node_id) {
        node_id_ = node_id;
    }

    void set_luminance(int luminance) {
        luminance_ = luminance;
    }

    Responder with_new_message_id() const {
        Responder responder   = *this;
        responder.message_id_ = generate_message_id();
        return responder;
    }

    bool empty() const {
        return identifiers_.empty() && message_id_.empty();
    }

    Responder& operator=(const Responder&) = default;

private:
    std::string send_response_impl(const std::string& data_serialized,
                                   MessageType        type,
                                   SendMode           send_mode,
                                   MessageStatus      status) const;

    MessageType message_type_;
    // MessageStatus                   message_status_;
    std::string                     ip_;
    std::unordered_set<std::string> identifiers_;
    std::string                     message_id_;
    NodeId                          node_id_;
    int                             luminance_      = 0;
    NetworkManager*                 network_manager = nullptr;
};

/**
 * @brief The NetworkManager class
 * Creates Discovery, Server and Sockets services
 */
class EXTRACHAIN_EXPORT NetworkManager : public QObject {
    Q_OBJECT

private:
    bool                            active_ = false;
    std::set<std::string>           failed_ips_;
    std::unique_ptr<UPNPConnection> upnp_dis_;
    std::unique_ptr<UPNPConnection> upnp_net_;
    std::unique_ptr<UPnPConnector>  upnp_connector_;
    QMap<std::string, int>          msg_hash_list_ = {};

    ExtraChainNode*                       node;
    std::shared_ptr<QNetworkAddressEntry> local_;

    // Boost.Asio networking
    std::unique_ptr<asio::io_context>  ioc_;
    std::unique_ptr<tcp::acceptor>     acceptor_;
    std::vector<std::thread>           io_threads_;
    std::atomic<bool>                  io_running_{false};

    SafePtr<std::set<SocketService::Ptr>>        connections_;
    SafePtr<std::map<NetworkReconnect, QString>> reconnections_to_identifier_;
    NetworkStatus                                network_status_;

    std::map<std::string, int> reconn_;

    SafePtr<std::map<std::string, std::pair<std::string, QDateTime>>>           messages_;
    std::map<std::string, MessageIdDataWaiting>                                 messages_waiting_;
    std::map<std::string, MessageIdDataReceived>                                messages_received_;
    QTimer*                                                                     reconnect_timer_;
    QTimer*                                                                     clear_network_caches_timer_;
    CalculateTraffic*                                                           calculate_traffic_;
    SafePtr<std::unordered_map<std::string, std::pair<std::string, QDateTime>>> forwarded_messages_;

    std::string network_hash_for_vpn_;

    std::string              public_ip_;
    std::vector<std::string> first_nodes_ =
#ifdef QT_DEBUG
        {
            "57.128.191.73", // test node 1
            "57.128.191.74"  // test node 2
        };
#else
        {
            "51.68.181.52", // release node 1
             // "149.33.19.250" // release node 2
        };
#endif
    std::string first_node_;

public:
    explicit NetworkManager(ExtraChainNode* node, std::uint16_t port);
    ~NetworkManager();
    void                        local_inizialization();
    std::pair<QString, QString> search_public_ip_and_country_(const QString& ip = "", bool alt = false);

    bool remove_one_connection();

    std::string getNetworkVPNHash() noexcept;
    void        setNetworkVPNHash() noexcept;

    // protected:
    // std::uint16_t tcp_port = 17593;
    std::uint16_t ws_port = 17593;

private:
    void setup_service_callbacks(SocketService::Ptr service, bool requestListNodes = false);

    // Asio coroutines
    VoidTask accept_loop();
    VoidTask do_connect_websocket(const std::string& ip, uint16_t port, bool requestListNodes, bool isConstant, bool is_light);

    void send_message_connections(const std::string& serialized_message,
                                  const MessageBody& non_serialized_message,
                                  SendMode           send_mode,
                                  const std::string& receiver_identifier,
                                  MessageType        message_type = MessageType::Unknown,
                                  MessageStatus      status_info  = MessageStatus::NoStatus);

    void clear_network_caches();

    void add_all_services_identifiers_to_message(MessageBody& msg);
    bool is_first_node(const std::string& identifier); // detect for safety

public:
    SafePtr<std::set<SocketService::Ptr>> connections() const;
    asio::io_context& io_context() { return *ioc_; }
    bool server_status(Network::Protocol protocol = Network::Protocol::WebSocket) const;
    void connect_network();

public slots:
    void remove_connection(const QString& identifier);
    void check_port(const QString ip, Network::Protocol protocol, const bool request, const bool isConstant);
    bool check_port_sync(const QString& ip, Network::Protocol protocol, const bool request, const bool isConstant);

signals:
    void finished(); // ThreadPool
    void connect_to_node(const QString&    ip,
                         Network::Protocol protocol,
                         const bool        request    = false,
                         const bool        isConstant = false,
                         const bool        is_light   = false);

protected:
    void connect_to_websocket(const QString& ip,
                              quint16        port,
                              bool           requestListNodes = false,
                              const bool     isConstant       = false,
                              const bool     is_light         = false);

    /**
     * @brief NetworkManager::check_message_count
     * @param msg
     * @return
     */
    bool check_message_count(const std::string& msg);

private slots:
    // onNewWsConnection replaced by accept_loop coroutine

protected slots:
    virtual void check_connections_status();
    void         start_discovery();

public slots:
    void start_network();
    void connect_to_node_slot(const QString&    ip,
                              Network::Protocol protocol,
                              const bool        request    = false,
                              bool              isConstant = false,
                              const bool        is_light   = false);
    void process();
    void reconnection();
    void setup_proxy(QNetworkProxy::ProxyType type,
                     const QString&           hostName,
                     quint16                  port,
                     const QString&           user,
                     const QString&           password);

private:
    void remove_socket_connection(SocketService::Ptr service);
    void socket_error(SocketService::Ptr          service,
                      Network::SocketServiceError error,
                      const std::string&          errorData,
                      const std::string&          identifier);

public:
    QString local_ip(); // TODO: remove

    void        initialize_first_node();
    std::string first_node();
    bool        save_first_node(const std::string_view first_node);

    void send_broadcast_message_further(const NetworkPackageStorage& package_data);

    void save_to_cache(const std::string& serialized_message,
                       SendMode           send_mode,
                       const std::string& receiver_identifier);
    void send_from_cache();
    bool is_connection_exists(const std::string& identifier);
    bool is_active_connection_exists();
    int  active_connections_count();

    VoidTask message_received(std::string message, std::string ip, std::string identifier);

    QString found_current_identifier(QString ip, quint16 port);

    bool        send_message_checker(MessageType      type,
                                     SendMode         send_mode,
                                     MessageStatus    status,
                                     const Responder& responder);
    std::string send_message_send(const std::string& data_serialized,
                                  MessageType        type,
                                  SendMode           send_mode,
                                  MessageStatus      status,
                                  const Responder&   responder);

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

        auto data_serialized = MessagePack::serialize(data);
        auto message_id      = send_message_send(data_serialized, type, send_mode, status, responder);
        return message_id;
    }

    template <class T>
    std::string send_broadcast(const T& data, MessageType type, MessageStatus status = MessageStatus::NoStatus) {
        auto message_id = send_message(data, type, SendMode::Broadcast, status);
        return message_id;
    }

    SafePtr<std::map<NetworkReconnect, QString>> reconnections();

    CalculateTraffic* calculate_traffic() const;

    std::string public_ip() const;
    void        set_public_ip(const std::string& newPublic_ip);

signals:
    void newSocketActivated();
    void newSocketActivatedWithParams(const std::string ip, const std::string identifier);
    void connectionStatusChanged(bool status);
    void connectionsCountChanged(int socketsCount);
    void connectionError(Network::SocketServiceError error, QString ip, QString identifier, QString errorData);
    void messageCountReceived(SectionId count);
    void customMessageReceived(const NetworkPackageStorage packageData, const CustomMessage customPackage);
    void messageReceivedSignal(const std::string& message, const std::string& ip, const std::string& identifier);
};
