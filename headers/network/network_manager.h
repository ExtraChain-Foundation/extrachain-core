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
#include <QtWebSockets/QWebSocketServer>
#include <string>
#include <shared_mutex>

#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "network/message_body.h"
#include "network/network_status.h"
#include "dfs/dfs_utils.h"
#include "utils/exc_utils.h"

class SocketService;
class WebSocketService;
class UPNPConnection;
class UPnPConnector;

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
    static CalculateTraffic* GetInstance();

    // Method for adding sent bytes data for a specific connection
    void addBytesSent(const std::string& ip, qint64 bytes);

    // Method for adding received bytes data for a specific connection
    void addBytesReceived(const std::string& connectionId, qint64 bytes);

    // Method for getting the total number of sent bytes data for a specific connection
    qint64 totalBytesSentFromConnection(const std::string& ip);

    // Method for getting the total number of received bytes data for a specific connection
    qint64 totalBytesReceivedFromConnection(const std::string& ip);

    // Method for gettint pair of sent and recieved bytes from all connections
    std::pair<uint64_t, uint64_t> totalBytes();
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

    const std::unordered_set<std::string>& identifiers() const {
        return identifiers_;
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

    void set_message_id(const std::string& message_id) {
        message_id_ = message_id;
    }

    void set_message_type(MessageType type) {
        message_type = type;
    }

    Responder with_new_message_id() const {
        Responder responder   = *this;
        responder.message_id_ = generate_message_id();
        return responder;
    }

    Responder& operator=(const Responder&) = default;

private:
    std::string send_response_impl(const std::string& data_serialized,
                                   MessageType        type,
                                   SendMode           send_mode,
                                   MessageStatus      status) const;

    MessageType message_type;
    // MessageStatus                   message_status;
    std::unordered_set<std::string> identifiers_;
    std::string                     message_id_;
    NetworkManager*                 network_manager = nullptr;
};

/**
 * @brief The NetworkManager class
 * Creates Discovery, Server and Sockets services
 */
class EXTRACHAIN_EXPORT NetworkManager : public QObject {
    Q_OBJECT

private:
    bool                            reservedActorListUse = false;
    bool                            active               = false;
    bool                            shouldRequest        = false;
    std::set<std::string>           failed_ips;
    std::unique_ptr<UPNPConnection> upnpDis;
    std::unique_ptr<UPNPConnection> upnpNet;
    std::unique_ptr<UPnPConnector>  upnpConnector;
    QMap<std::string, int>          msgHashList = {};

    ExtraChainNode*                              node;
    std::shared_ptr<QNetworkAddressEntry>        local;
    QWebSocketServer*                            wsServer = nullptr;
    SafePtr<std::set<SocketService*>>            m_connections;
    SafePtr<std::map<NetworkReconnect, QString>> m_reconnectionsToIdentifier;
    NetworkStatus                                m_networkStatus;

    std::map<std::string, int> reconn_;

    SafePtr<std::map<std::string, std::pair<std::string, QDateTime>>>           m_messages;
    std::map<std::string, MessageIdDataWaiting>                                 m_messages_waiting;
    std::map<std::string, MessageIdDataReceived>                                m_messages_received;
    QTimer*                                                                     m_reconnectTimer;
    QTimer*                                                                     m_clear_network_caches_timer;
    CalculateTraffic*                                                           calculateTraffic;
    SafePtr<std::unordered_map<std::string, std::pair<std::string, QDateTime>>> m_network_forwarded_messages;

    std::string m_networkHashForVPN;

    std::string public_ip_;
    std::string first_node_ =
#ifdef QT_DEBUG
        "57.128.191.73"; // test node
#else
        "51.68.181.52"; // exc node
#endif

public:
    explicit NetworkManager(ExtraChainNode* node);
    ~NetworkManager();
    void                        localInizialization();
    std::pair<QString, QString> getPublicIPAndCountry(const QString& ip = "", bool alt = false);

    bool removeOneConnection();

    std::string getNetworkVPNHash() noexcept;
    void        setNetworkVPNHash() noexcept;

    // protected:
    // quint16 tcpPort = 2222;
    const quint16 wsPort = 2222;

private:
    void connectWsService(WebSocketService* ws, bool requestListNodes = false);

    void send_message_connections(const std::string& serialized_message,
                                  const MessageBody& non_serialized_message,
                                  SendMode           send_mode,
                                  const std::string& receiver_identifier,
                                  MessageType        message_type = MessageType::Unknown,
                                  MessageStatus      status_info  = MessageStatus::NoStatus);

    void clearNetworkCaches();

    void addAllServicesIdentifiersToMessage(MessageBody& msg);

public:
    SafePtr<std::set<SocketService*>> connections() const;
    bool serverStatus(Network::Protocol protocol = Network::Protocol::WebSocket) const;
    void connect_network() {
        connectToNode(QString::fromStdString(first_node_), Network::Protocol::WebSocket);
    }

public slots:
    void removeConnection(const QString& identifier);

    void checkPort(const QString ip, Network::Protocol protocol, const bool request, const bool isConstant);

signals:
    void finished(); // ThreadPool
    void connectToNode(const QString&    ip,
                       Network::Protocol protocol,
                       const bool        request    = false,
                       const bool        isConstant = false);

protected:
    void connectToWebSocket(const QString& ip,
                            quint16        port,
                            bool           requestListNodes = false,
                            const bool     isConstant       = false);

    /**
     * @brief NetworkManager::checkMsgCount
     * @param msg
     * @return
     */
    bool checkMsgCount(const std::string& msg);

private slots:
    void onNewWsConnection();

protected slots:
    virtual void checkConnectionsStatus();
    void         startDiscovery();

public slots:
    void startNetwork();
    void connectToNodeSlot(const QString&    ip,
                           Network::Protocol protocol,
                           const bool        request    = false,
                           bool              isConstant = false);
    void process();
    void reconnection();
    void setupProxy(QNetworkProxy::ProxyType type,
                    const QString&           hostName,
                    quint16                  port,
                    const QString&           user,
                    const QString&           password);

private slots:
    void removeWsConnection();
    void socketError(Network::SocketServiceError error, QString errorData, std::string ip, std::string identifier);

public:
    QString localIp(); // TODO: remove

    void        initialize_first_node();
    std::string first_node();
    bool        save_first_node(const std::string_view first_node);

    void sendBrodcastMessageFurther(const NetworkPackageStorage& package_data);

    void saveToCache(const std::string& serialized_message,
                     SendMode           send_mode,
                     const std::string& receiver_identifier);
    void sendFromCache();
    bool is_connection_exists(const std::string& identifier);
    bool isActiveConnectionExists();
    int  active_connections_count();

    void messageReceived(const std::string& message, const std::string& ip, const std::string& identifier);

    QString foundCurrentIdentifier(QString ip, quint16 port);

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

    CalculateTraffic* getCalculateTraffic() const;

    std::string public_ip() const;
    void        set_public_ip(const std::string& newPublic_ip);

signals:
    void newSocketActivated();
    void newSocketActivatedWithParams(const std::string ip, const std::string identifier);
    void connectionStatusChanged(bool status);
    void connectionsCountChanged(int socketsCount);
    void connectionError(Network::SocketServiceError error, QString ip, QString identifier, QString errorData);
    void messageCountReceived(BigNumber count);
    void customMessageReceived(const NetworkPackageStorage packageData, const CustomMessage customPackage);
    void messageReceivedSignal(const std::string& message, const std::string& ip, const std::string& identifier);
};
