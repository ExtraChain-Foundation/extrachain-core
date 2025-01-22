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

class Responder {
public:
    // send
    // broadcast send

private:
    std::string     identifier;
    std::string     message_id;
    NetworkManager* network_manager;
};

static const std::string NetworkCacheFile = "tmp/network.cache";

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
    QMap<std::string, int>          msgHashList = {};

    ExtraChainNode*                              node;
    std::shared_ptr<QNetworkAddressEntry>        local;
    QWebSocketServer*                            wsServer = nullptr;
    SafePtr<std::set<SocketService*>>            m_connections;
    SafePtr<std::map<NetworkReconnect, QString>> m_reconnectionsToIdentifier;
    NetworkStatus                                m_networkStatus;

    SafePtr<std::map<std::string, std::pair<std::string, QDateTime>>>           m_messages;
    std::map<std::string, MessageIdDataWaiting>                                 m_messages_waiting;
    std::map<std::string, MessageIdDataReceived>                                m_messages_received;
    QTimer*                                                                     m_reconnectTimer;
    QTimer*                                                                     m_clear_network_caches_timer;
    CalculateTraffic*                                                           calculateTraffic;
    SafePtr<std::unordered_map<std::string, std::pair<std::string, QDateTime>>> m_network_forwarded_messages;

    std::string m_networkHashForVPN;

public:
    explicit NetworkManager(ExtraChainNode* node);
    ~NetworkManager();
    void                        localInizialization();
    std::pair<QString, QString> getPublicIPAndCountry();
    bool                        removeOneConnection();

    std::string getNetworkVPNHash() noexcept;
    void        setNetworkVPNHash() noexcept;

    // protected:
    // quint16 tcpPort = 2222;
    const quint16 wsPort = 2222;

private:
    void connectWsService(WebSocketService* ws, bool requestListNodes = false);

    void sendMessage(const std::string&    serialized_message,
                     const MessageBody&    non_serialized_message,
                     Config::Net::TypeSend typeSend,
                     const std::string&    receiver_identifier,
                     MessageType           message_type = MessageType::Unknown,
                     MessageStatus         status_info  = MessageStatus::NoStatus);

    void clearNetworkCaches();

    void addAllServicesIdentifiersToMessage(MessageBody& msg);

public:
    SafePtr<std::set<SocketService*>> connections() const;
    bool serverStatus(Network::Protocol protocol = Network::Protocol::WebSocket) const;

public slots:
    void removeConnection(const QString& identifier);

signals:
    void finished(); // ThreadPool

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
    void connectToNode(const QString&    ip,
                       Network::Protocol protocol,
                       const bool        request    = false,
                       const bool        isConstant = false);
    void process();
    void reconnection();
    void reconnectSocket(const NetworkReconnect& connectInfo, QString identifier);
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

    void sendBrodcastMessageFurther(const NetworkPackageStorage& package_data);

    void saveToCache(const std::string&    serialized_message,
                     Config::Net::TypeSend typeSend,
                     const std::string&    receiver_identifier);
    void sendFromCache();
    bool isActiveConnectionExists();

    void messageReceived(const std::string& message, const std::string& ip, const std::string& identifier);

    QString foundCurrentIdentifier(QString ip, quint16 port);

    template <class T>
    std::string send_message(T                     data,
                             MessageType           type,
                             Config::Net::TypeSend typeSend,
                             MessageStatus         status              = MessageStatus::NoStatus,
                             std::string           to_message_id       = "",
                             std::string           receiver_identifier = "") {
        if (status == MessageStatus::Response && to_message_id.empty() && receiver_identifier.empty()) {
            eCritical(
                "[Network] Send message error: empty message id or receiver identifier for response message");
            return "";
        }
        if (status == MessageStatus::Response && typeSend != Config::Net::TypeSend::Focused) {
            eWarning(
                "[Network] Send message warning: incorrect type send for response message, set to focused, type: "
                "{}",
                type);
            typeSend = Config::Net::TypeSend::Focused;
        }

        if (!node) {
            eCritical("[Network] Send message error: accountController is bye 1!");
            return "";
        }
        if (!node->accountController()) {
            eCritical("[Network] Send message error: accountController is bye 2!");
            return "";
        }
        if (node->accountController()->empty()) {
            eCritical("[Network] Send message error: accountController is empty!");
            return "";
        }

        auto&       mainActor = node->accountController()->mainActor();
        MessageBody message =
            make_init_message(MessagePack::serialize(data), typeSend, type, status, mainActor.id(), to_message_id);

        if (typeSend == Config::Net::TypeSend::Broadcast) {
            addAllServicesIdentifiersToMessage(message);
        }

        auto serialized          = message.serialize();
        auto serialized_for_sign = message.serializeForSign();
        auto sign_result         = mainActor.key().sign(ByteArray(serialized_for_sign).toBytes());
        if (!sign_result.has_value()) {
            return "";
        }
        auto sign = ByteArray(sign_result.value()).toString();
        if (!to_message_id.empty()) {
            auto messages_locked = *m_messages;
            if (messages_locked->count(to_message_id)) {
                receiver_identifier = messages_locked->at(to_message_id).first;
            } else {
                eWarning("[Network Message] Can't send message, because no to_message_id in m_messages: {}",
                         to_message_id);
                return "";
            }
            //            if (receiver_identifier.empty())
            //                eFatal("Network send message error: receiver_identifier is empty");
            // m_messages.erase(to_message_id);
        }

#ifdef QT_DEBUG
        if (Network::networkDebug) {
            msgpack::object_handle oh           = msgpack::unpack(serialized.data(), serialized.size());
            msgpack::object        deserialized = oh.get();
            eLog("[Network Message] Send: type {}, status {}, id {}, type send {}, body: {}",
                 message.message_type,
                 message.status,
                 message.message_id,
                 typeSend,
                 (std::stringstream() << deserialized).str());
        }
#endif

        this->sendMessage(serialized + sign, message, typeSend, receiver_identifier, type, status);

        return message.message_id;
    }

    SafePtr<std::map<NetworkReconnect, QString>> reconnections();

    CalculateTraffic* getCalculateTraffic() const;

signals:
    void newSocketActivated();
    void newSocketActivatedWithParams(const std::string ip, const std::string identifier);
    void connectionStatusChanged(bool status);
    void connectionsCountChanged(int socketsCount);
    void connectionError(Network::SocketServiceError error, QString ip, QString identifier, QString errorData);
    void messageCountReceived(BigNumber count);
    void customMessageReceived(const NetworkPackageStorage packageData, const CustomMessage customPackage);

    friend class DfsNetworkManager;
};
