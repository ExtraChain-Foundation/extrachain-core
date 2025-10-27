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
#include "dfs/dfs_controller.h"
#include "managers/data_mining_manager.h"
#include "managers/extrachain_node.h"
#include "managers/luminance_manager.h"
#include "network/upnpconnection.h"
#include "network/upnpconnector.h"
#include "network/websocket_service.h"
#include "utils/exc_logs.h"
#include "dfs/historical_collection.h"
#include "dfs/dirs_manager.h"
#include "utils/thread_pool_boost.h"

#include <filesystem>
#include <fstream>
#include <vector>

#include <QJsonObject>

CalculateTraffic *CalculateTraffic::calculateTraffic_ = nullptr;

SafePtr<std::set<SocketService *>> NetworkManager::connections() const {
    return connections_;
}

bool NetworkManager::server_status(Network::Protocol protocol) const {
    switch (protocol) {
    case Network::Protocol::Udp:
        break;
    case Network::Protocol::WebSocket:
        return ws_server_ == nullptr ? false : ws_server_->isListening();
    case Network::Protocol::Undefined:
        return false;
    }
    return false;
}

SafePtr<std::map<NetworkReconnect, QString>> NetworkManager::reconnections() {
    return reconnections_to_identifier_;
}
CalculateTraffic *NetworkManager::calculate_traffic() const {
    return calculate_traffic_;
}

std::string NetworkManager::public_ip() const {
    return public_ip_;
}

void NetworkManager::set_public_ip(const std::string &new_public_ip) {
    if (new_public_ip.empty()) {
        return;
    }

    QHostAddress address(QString::fromStdString(new_public_ip));
    if (address.isNull() || address.isSiteLocal() || address.isLoopback()) {
        return;
    }

    public_ip_ = new_public_ip;

#ifdef Q_OS_LINUX
    return;
#endif

    if (node->init_public_ip_and_country().first.isEmpty()) {
        node->init_public_ip_and_country_ = { QString::fromStdString(public_ip_), "Security" };
    }
}

NetworkManager::NetworkManager(ExtraChainNode *node)
    : QObject(node)
    , node(node) {
    local_inizialization();
    initialize_first_node();

    reconnect_timer_            = new QTimer(this);
    clear_network_caches_timer_ = new QTimer(this);
    calculate_traffic_          = CalculateTraffic::get_instance();

    connect(clear_network_caches_timer_, &QTimer::timeout, this, &NetworkManager::clear_network_caches);
    clear_network_caches_timer_->start(20000);

    process();

    connect(this, &NetworkManager::connect_to_node, this, &NetworkManager::check_port);

    connect(this, &NetworkManager::messageReceivedSignal, this, &NetworkManager::message_received);

    /*
    QTimer::singleShot(20000, [this]() {
        std::string a = Network::currentIdentifier().toStdString();
        eLog("[WS] Current Identifier print: {}", a);

         auto connectionsLocked = *m_connections;
         for (const auto &service : *connectionsLocked) {
             std::string b = service->identifier().toStdString();
             eLog("[WS] Service ident: {}", b);
         }
     });
     */
}

void NetworkManager::add_all_services_identifiers_to_message(MessageBody &msg) {
    for (const auto &it : msg.nodes_identifiers_to_ignore_later) {
        msg.nodes_identifiers_to_ignore.emplace(it);
    }
    msg.nodes_identifiers_to_ignore_later.clear();

    msg.nodes_identifiers_to_ignore_later.emplace(node->node_identifier());

    auto connectionsLocked = *connections_;
    for (const auto &service : *connectionsLocked) {
        std::string ident = service->identifier().toStdString();

        if (!ident.empty())
            msg.nodes_identifiers_to_ignore_later.emplace(ident);
    }
}

bool NetworkManager::is_first_node(const std::string &identifier) {
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

void NetworkManager::process() {
    if (!node->is_client_application())
        return;

    connect(reconnect_timer_, &QTimer::timeout, this, &NetworkManager::reconnection);
    reconnect_timer_->start(Utils::RECONNECT_INTERVAL);
}

void NetworkManager::reconnection() {
    if (node->account_controller()->empty()) {
        return;
    }

    if (failed_ips_.contains(first_node_)) {
        return;
    }

    // if (first_node_ == localIp().toStdString()) {
    //     m_reconnectTimer->stop();
    //     return;
    // }

    bool                      skip_first_node = false;
    auto                      need_reconnect  = reconn_;
    std::set<SocketService *> to_close;

    {
        auto connectionsLocked = *connections_;
        for (const auto &el : *connectionsLocked) {
            // eLog("_____________");
            if (el->is_closed()) {
                // if (Utils::current_date_ms() - el->timestamp() > 10000) {
                //     s.insert(el);
                // }
                continue;
            }

            if (el->ip() == first_node_) {
                bool is_early = Utils::current_date_ms() - el->timestamp() < 30000;

                if (!is_early && !el->is_active()) {
                    skip_first_node = false;
                    to_close.insert(el);
                    break;
                } else {
                    skip_first_node = true;
                }
            }

            if (el->timestamp() != 0 && need_reconnect.contains(el->ip().toStdString())) {
                need_reconnect.erase(el->ip().toStdString());
            }

            if (el->timestamp() != 0 && !el->is_active() && Utils::current_date_ms() - el->timestamp() > 30000) {
                // eLog("PHYYYY {}", Utils::current_date_ms() - el->timestamp());
                // to_close.insert(el);
            }
        }
    }

    for (const auto &el : to_close) {
        emit el->close(Network::SocketServiceError::Secs10Inactive);
    }

    if (!skip_first_node) {
        eLog("[Network] Reconnect to first node {}", first_node_);
        emit connect_to_node(QString::fromStdString(first_node_), Network::Protocol::WebSocket);
        return;
    }

    for (const auto &[ip, count] : need_reconnect) {
        eLog("[Network] Reconnect to node: {}", ip);

        if (failed_ips_.contains(ip)) {
            continue;
        }

        emit connect_to_node(QString::fromStdString(ip), Network::Protocol::WebSocket);
        // reconn_[ip] += 1; // count

        // if (reconn_[ip] > 1000) {
        //     reconn_.erase(ip);
        // }
    }
}

void NetworkManager::setup_proxy(QNetworkProxy::ProxyType type,
                                 const QString           &hostName,
                                 quint16                  port,
                                 const QString           &user,
                                 const QString           &password) {
    QNetworkProxy proxy;
    proxy.setType(type);
    proxy.setHostName(hostName);
    proxy.setPort(port);
    proxy.setUser(user);
    proxy.setPassword(password);
    QNetworkProxy::setApplicationProxy(proxy);
}

void NetworkManager::connectWsService(WebSocketService *service, bool requestListNodes) {
    connect(service, &WebSocketService::error, this, &NetworkManager::socket_error);
    connect(service, &WebSocketService::disconnected, this, &NetworkManager::remove_socket_connection);
    connect(service, &WebSocketService::activated, this, &NetworkManager::check_connections_status);
    connect(service, &WebSocketService::activated, this, [&] {
        auto senderObj = QObject::sender();
        if (senderObj == nullptr)
            return;

        auto service = qobject_cast<SocketService *>(senderObj);

        emit this->newSocketActivatedWithParams(service->ip().toStdString(), service->identifier().toStdString());
        emit this->newSocketActivated();

        if (service->mode() == SocketMode::Full && service->ip() != first_node()) {
            reconn_.insert({ service->ip().toStdString(), 1 });
        }
    });

    {
        auto connectionsLocked = *connections_;
        if (!connectionsLocked->contains(service))
            connectionsLocked->insert(service);
    }
    connect(service,
            &WebSocketService::shareConnections,
            this,
            [&](const std::set<SocketService::SocketPair> &connections) {
                // eLog("shareConnections: {}", connections);

                auto init_ip = node->init_public_ip_and_country().first;

                /*
                // for tests
                std::set<std::string> ips;
                for (const auto &pair : connections) {
                    ips.insert(pair.ip);
                }
                eLog("{}", ips);
                */

                if (active_connections_count() >= Network::maxConnections) {
                    eLog("shareConnections ignored by max connections limit");

                    if (init_ip != first_node_) {
                        return;
                    }
                }

                for (const auto &[ip, identifier] : connections) {
                    bool can_connect = true;

                    {
                        auto connections_locked = *connections_;
                        for (const auto &conn_item : *connections_locked) {
                            if (ip == init_ip) {
                                can_connect = false;
                                break;
                            }

                            if (identifier == node->node_identifier()) {
                                can_connect = false;
                                break;
                            }

                            if (conn_item->identifier() == identifier) {
                                can_connect = false;
                                break;
                            }

                            if (conn_item->ip() == ip) {
                                can_connect = false;
                                break;
                            }
                        }
                    }

                    if (can_connect) {
                        emit connect_to_node(QString::fromStdString(ip), Network::Protocol::WebSocket);
                    }
                }
            });
}

void NetworkManager::remove_connection(const QString &identifier) {
    if (identifier.isEmpty())
        eFatal("Try remove with empty identifier");
    auto connectionsLocked = *connections_;
    for (const auto &connection : *connectionsLocked) {
        if (connection->identifier() == identifier)
            emit connection->close();
    }
}

void NetworkManager::check_port(const QString     ip,
                                Network::Protocol protocol,
                                const bool        request,
                                const bool        isConstant) {
    // if (active_connections_count() > Network::maxConnections) {
    //     return;
    // }

    // int         timeoutMs = 1000;
    QTcpSocket *socket = new QTcpSocket(this);

    connect(socket, &QTcpSocket::connected, this, [this, socket, ip, protocol, request, isConstant]() {
        socket->disconnectFromHost();
        socket->deleteLater();
        // emit portCheckResult(ip, port, true);
        connect_to_node_slot(ip, protocol, request, isConstant);
    });

    connect(socket, &QTcpSocket::errorOccurred, this, [this, socket, ip](QAbstractSocket::SocketError error) {
        socket->deleteLater();
    });

    // QTimer* timer = new QTimer(this);
    // timer->setSingleShot(true);
    // connect(timer, &QTimer::timeout, this, [this, socket, timer, ip]() {
    //     if (socket->state() == QAbstractSocket::ConnectingState) {
    //         socket->abort();
    //         // emit portCheckResult(ip, wsPort, false);
    //         socket->deleteLater();
    //     }
    //     timer->deleteLater();
    // });

    socket->connectToHost(QHostAddress(ip), wsPort);
    // timer->start(timeoutMs);
}

NetworkManager::~NetworkManager() {
    eLog("[NetworkManager] Finish him with {} connections", connections_->size());

    std::set<SocketService *> copied;
    {
        auto connectionsLocked = *connections_;
        copied                 = **connections_;
    }

    for (const auto &connection : copied) {
        connection->flush();
        emit connection->close();
    }
}

void NetworkManager::check_connections_status() {
    std::unordered_set<std::string> ind_temp;
    // m_reconnectTimer->stop();
    bool flag  = false;
    int  count = 0;
    {
        auto connectionsLocked = *connections_;
        std::for_each(connectionsLocked->begin(), connectionsLocked->end(), [&](SocketService *el) {
            flag = flag || el->is_active();
            if (el->is_active()) {
                count++;
                ind_temp.insert(el->identifier().toStdString());
            }
        });
    }
    emit connectionStatusChanged(flag);
    emit connectionsCountChanged(count); // TODO: check prev count value
}

void NetworkManager::start_network() {
    eLog("[NetworkManager] Start servers... {}", (wsPort == 17593 ? "Network" : "Else"));

    if (!local_) {
        eLog("[NetworkManager] Can't detect local ip");
        return;
    }

    if (!Network::isStartedServer)
        return;

    ws_server_ = new QWebSocketServer("ExtraChain", QWebSocketServer::SslMode::NonSecureMode);

    if (!ws_server_->listen(QHostAddress::Any, wsPort)) {
        eLog("[NetworkManager] Can't listen port {}", wsPort);
        return;
    }

    connect(ws_server_, &QWebSocketServer::newConnection, this, &NetworkManager::onNewWsConnection);
    connect(ws_server_, &QWebSocketServer::serverError, [](QWebSocketProtocol::CloseCode closeCode) {
        eLog("[WS] Server error code: {}", int(closeCode));
    });
    connect(ws_server_, &QWebSocketServer::closed, [] {
        eLog("[WS] Server: closed");
    });
    connect(ws_server_, &QWebSocketServer::acceptError, [](QAbstractSocket::SocketError socket_error) {
        eLog("[WS] Server socker error: {}", int(socket_error));
    });

    eLog("[WS] Start listening: {}:{}", ws_server_->serverAddress(), ws_server_->serverPort());
}

[[maybe_unused]] void NetworkManager::start_discovery() {
    eLog("NetworkManager::startDiscovery()");
    // discoveryService = new DiscoveryService(extPort, tcpPort, local);
    // ThreadPool::addThread(discoveryService);
    // connect(discoveryService, &DiscoveryService::ClientDiscovered, this,
    // &NetworkManager::addConnectionFromPair);
}

void NetworkManager::connect_to_node_slot(const QString    &ip,
                                          Network::Protocol protocol,
                                          const bool        request,
                                          bool              isConstant,
                                          const bool        is_light) {
    if (ip.toStdString() == first_node_) {
        isConstant = true;
    }

    if (active_connections_count() >= Network::maxConnections) {
        if (isConstant && !remove_one_connection()) {
            eLog("[NetworkManager] Can't connect because the maximum number of connections");
            return;
        }
    }

    if (ip.isEmpty()) {
        // eLog("Ip is empty");
        return;
    }

    const quint16 port = (protocol == Network::Protocol::WebSocket ? wsPort : 0);
    eLog("[NetworkManager] Connect to {}, protocol: {}, port: {}", ip, Utils::enum_value_name(protocol), port);
    // m_reconnections.insert(NetworkReconnect { .ip = ip, .port = port, .protocol = protocol });

    using Network::Protocol;
    switch (protocol) {
    case Protocol::Udp:
        break;
    case Protocol::WebSocket:
        connect_to_websocket(ip.simplified(), port, request, isConstant, is_light);
        break;
    case Protocol::Undefined:
        eFatal("Undefined connectToNode");
    }
}

void NetworkManager::connect_to_websocket(const QString &ip,
                                          quint16        port,
                                          bool           requestListNodes,
                                          const bool     isConstant,
                                          const bool     is_light) {
    if (ip.isEmpty()) {
        return;
    }

    auto service = new WebSocketService(nullptr, node, this, isConstant, is_light);
    connectWsService(service, requestListNodes);
    service->open(ip, port);
    reconnections_to_identifier_
        ->emplace(NetworkReconnect { .ip = ip, .port = port, .protocol = Network::Protocol::WebSocket }, "");
}

void NetworkManager::clear_network_caches() {
    {
        auto network_forwarded_messages_locked = *forwarded_messages_;
        for (auto it = network_forwarded_messages_locked->begin();
             it != network_forwarded_messages_locked->end();) {
            QDateTime currentTime = QDateTime::currentDateTime();
            if (it->second.second.secsTo(currentTime) >= 120) {
                it = network_forwarded_messages_locked->erase(it);
            } else
                ++it;
        }
    }

    {
        auto messages_locked = *messages_;
        for (auto it = messages_locked->begin(); it != messages_locked->end();) {
            QDateTime currentTime = QDateTime::currentDateTime();
            if (it->second.second.secsTo(currentTime) >= 120) {
                // eTemp("MessageID erased: {}", it->first);
                it = messages_locked->erase(it);
            } else
                ++it;
        }
    }
}

bool NetworkManager::send_message_checker(MessageType      type,
                                          SendMode         send_mode,
                                          MessageStatus    status,
                                          const Responder &responder) {
    if (status == MessageStatus::Response && responder.message_id().empty() && responder.identifiers().empty()) {
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
    if (status == MessageStatus::Response && send_mode != SendMode::Focused) {
        eWarning(
            "[Network] Send message warning: incorrect type send for response message, set to focused, "
            "type: "
            "{}",
            type);
        send_mode = SendMode::Focused;
    }

    return true;
}

std::string NetworkManager::send_message_send(const std::string &data_serialized,
                                              MessageType        type,
                                              SendMode           send_mode,
                                              MessageStatus      status,
                                              const Responder   &responder) {
    auto       &main_actor = node->account_controller()->system_actor();
    MessageBody message    = make_init_message(data_serialized,
                                            send_mode,
                                            type,
                                            status,
                                            main_actor.id(),
                                            responder.message_id(),
                                            node->node_identifier());

    if (send_mode == SendMode::Broadcast) {
        this->add_all_services_identifiers_to_message(message);
    }

    auto serialized      = message.serialize();
    auto serialized_hash = message.calculate_hash();
    auto sign_result     = main_actor.key().sign(ByteArray(serialized_hash).toBytes());
    if (!sign_result.has_value()) {
        return "";
    }

    auto sign = ByteArray(sign_result.value()).toString();

    std::string to_message_id = responder.message_id();
    std::string receiver_identifier;
    if (!to_message_id.empty()) {
        auto messages_locked = *messages_;
        if (messages_locked->count(to_message_id)) {
            receiver_identifier = messages_locked->at(to_message_id).first;
        } else {
            // eWarning("[Network Message] Can't send message, because no to_message_id in m_messages: {}",
            //          to_message_id);
            // return "";
        }
        //            if (receiver_identifier.empty())
        //                eFatal("Network send message error: receiver_identifier is empty");
        // m_messages.erase(to_message_id);
    }

    if (!responder.identifiers().empty()) {
        receiver_identifier = *responder.identifiers().begin();
    }

#ifdef QT_DEBUG
    if (Network::networkDebug) {
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

    this->send_message_connections(serialized + sign,
                                   message,
                                   send_mode,
                                   receiver_identifier,
                                   // responder.identifiers().empty() ? "" : *responder.identifiers().begin(),
                                   type,
                                   status);

    return message.message_id;
}

void NetworkManager::send_message_connections(const std::string &serialized_message,
                                              const MessageBody &non_serialized_message,
                                              SendMode           send_mode,
                                              const std::string &receiver_identifier,
                                              MessageType        message_type,
                                              MessageStatus      status_info) {
    if (!is_active_connection_exists()) {
        // eLog("[NetworkManager] Save message to cache {} {}", message_type, status_info);
        save_to_cache(serialized_message, send_mode, receiver_identifier);
        return;
    }

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
                // eTemp("[VPN] brocast further to socket: {}", socket_identifier);
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

    if (message_type == MessageType::Custom || message_type == MessageType::NewActor
        || message_type == MessageType::DagLightData
        || message_type == MessageType::DagSyncLastInfo) { // if client
        priority = SocketService::Priority::High;
    }

    auto connections_locked = *connections_;

    if (send_mode == SendMode::NeighboursRandom || send_mode == SendMode::OneNeighbourRandom) {
        std::vector<SocketService *> active_identifiers;
        const int                    randoms = send_mode == SendMode::NeighboursRandom ? 3 : 1;

        for (auto service : *connections_locked) {
            if (service->is_active()) {
                active_identifiers.push_back(service);
            }
        }

        auto indexes = Utils::random_indices<3>(active_identifiers.size());
        if (send_mode == SendMode::NeighboursRandom && active_identifiers.size() > 3) {

            for (int index : indexes) {
                active_identifiers[index]->send_message(QByteArray::fromStdString(serialized_message), priority);
            }

            return;
        }

        if (send_mode == SendMode::OneNeighbourRandom) {
            active_identifiers[indexes[0]]->send_message(QByteArray::fromStdString(serialized_message), priority);
        }
    }

    if (serialized_message.size() > 10000
        && (non_serialized_message.message_type != MessageType::DfsFileExistNotification
            && non_serialized_message.message_type != MessageType::DfsFileFragment)) {
        eTemp("Message: BIG {} {}", serialized_message.size(), non_serialized_message.message_type);
    }

    TIMER_START(kkk)

    for (const auto &service : *connections_locked) {
        if (!service->is_active()) {
            continue;
        }

        if (service->mode() == SocketMode::Light && send_mode != SendMode::Focused) {
            continue;
        }

        bool send_checked = is_send_check(send_mode,
                                          receiver_identifier,
                                          service->identifier().toStdString(),
                                          non_serialized_message);

        if (send_checked) {
            calculate_traffic_->add_bytes_sent(service->ip().toStdString(), serialized_message.size());
            service->send_message(QByteArray::fromStdString(serialized_message), priority);
            if (send_mode == SendMode::Focused) {
                break;
            }
        }
    }

    auto k = kkk.elapsed();
    if (k > 5) {
        eLog("____ send {} ms {}", k, message_type);
    }
}

void NetworkManager::send_broadcast_message_further(const NetworkPackageStorage &package_data) {
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
    send_message_connections(serialized + package_data.sign, message_edited, SendMode::Broadcast, "");

    // eTemp("Message forwarded with messageId: {}", package_data.msg_body.message_id);

    network_forwarded_messages_locked->emplace(message_edited.message_id,
                                               std::make_pair(package_data.prev_identifier,
                                                              QDateTime::currentDateTime()));
}

void NetworkManager::save_to_cache(const std::string &serialized_message,
                                   SendMode           send_mode,
                                   const std::string &receiver_identifier) {
    return;
    if (send_mode != SendMode::Broadcast) {
        // return;
    }

    std::ofstream file;
    file.open(NetworkCacheFile, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
    if (!file.is_open()) {
        eFatal("[NetworkManager/saveToCache] Error open cache file");
    }
    auto size = std::filesystem::file_size(NetworkCacheFile);

    if (size == 0) {
        std::string serializedMessage =
            Serialization::serialize(std::vector<std::string> { serialized_message,
                                                                Utils::enum_value_name_value(send_mode),
                                                                receiver_identifier });
        file << Serialization::serialize(std::vector<std::string> { serializedMessage });
        file.flush();
        file.close();
    } else {
        std::ifstream inputFile;
        inputFile.open(NetworkCacheFile, std::ios::binary);
        std::string data;
        if (inputFile.is_open()) {
            inputFile >> data;
        }
        inputFile.close();

        std::vector<std::string> list = Serialization::deserialize(data);
        std::string              serializedMessage =
            Serialization::serialize(std::vector<std::string> { serialized_message,
                                                                Utils::enum_value_name_value(send_mode),
                                                                receiver_identifier });
        list.push_back(serializedMessage);
        file.close();
        file.open(NetworkCacheFile, std::ofstream::out | std::ofstream::trunc);
        file << Serialization::serialize(list);
        file.close();
    }
}

void NetworkManager::send_from_cache() {
    QFile filet(QString::fromStdString(NetworkCacheFile));
    if (filet.exists()) {
        filet.remove();
    }
    return;
    eLog("[NetworkManager] Load from cache");

    QFile file(QString::fromStdString(NetworkCacheFile));
    if (!file.exists() || !file.open(QFile::ReadOnly)) {
        return;
    }

    std::vector<std::string> allPackages = Serialization::deserialize(file.readAll().toStdString());
    file.close();
    file.remove();

    for (const auto &item : allPackages) {
        const std::vector<std::string> deserializedList = Serialization::deserialize(item);
        if (deserializedList.size() < 3) {
            eWarning("Size deserialized data in not correct");
            continue;
        }

        const std::string deserialized_message = deserializedList[0];
        MessageBody       message_body     = MessagePack::deserialize<MessageBody>(deserialized_message).value();
        auto              send_mode_result = magic_enum::enum_cast<SendMode>(deserializedList[1]);

        if (!send_mode_result.has_value()) {
            eWarning("[SendFromCache] Incorrecnt send mode type");
            continue;
        }

        const SendMode    send_mode           = send_mode_result.value();
        const std::string receiver_identifier = deserializedList[2];

        send_message_connections(deserialized_message,
                                 message_body,
                                 send_mode,
                                 receiver_identifier,
                                 MessageType::Unknown,
                                 MessageStatus::NoStatus);
    }
}

bool NetworkManager::is_connection_exists(const std::string &identifier) {
    auto connections_locked = *connections_;
    for (const auto &service : *connections_locked) {
        if (!service->is_active()) {
            continue;
        }
        if (service->identifier().toStdString() == identifier) {
            return true;
        }
    }

    return false;
}

bool NetworkManager::is_active_connection_exists() {
    auto connectionsLocked = *connections_;
    if (connectionsLocked->empty()) {
        return false;
    }

    for (const auto &el : *connectionsLocked) {
        if (el->is_active()) {
            return true;
        }
    }

    return false;
}

int NetworkManager::active_connections_count() {
    auto connectionsLocked = *connections_;
    if (connectionsLocked->empty()) {
        return 0;
    }

    int count = 0;
    for (const auto &el : *connectionsLocked) {
        if (el->is_active()) {
            count++;
        }
    }

    return count;
}

bool NetworkManager::check_message_count(const std::string &msg) {
    bool                             flag_result = true;
    bool                             value       = 0;
    std::string                      hashMsg     = Utils::calculate_hash(msg);
    QMap<std::string, int>::iterator it          = msg_hash_list_.find(hashMsg);

    if (it == msg_hash_list_.end())
        msg_hash_list_.insert(hashMsg, value);
    else {
        if (msg_hash_list_.find(hashMsg).value() == connections_->size() - 1) {
            msg_hash_list_.remove(hashMsg);
            flag_result = false;
        } else {
            msg_hash_list_.find(hashMsg).value()++;
            flag_result = true;
        }
    }

    return flag_result;
}

void NetworkManager::message_received(const std::string &message,
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
        eWarning("[NetworkManager] message_received: can't deserialize message body");
        return;
    }

    MessageBody message_body = message_body_expected.value();
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

    SendMode      send_type  = message_body.send_type;
    MessageType   type       = message_body.message_type;
    MessageStatus status     = message_body.status;
    std::string   serialized = message_body.data;
    std::string   mess_id    = message_body.message_id;
    std::string   message_id(mess_id.begin(), mess_id.end());
    bool          is_luminance = node_id.actor_id == node->network_id();

    if (status == MessageStatus::Request || status == MessageStatus::NoStatus) {
        bool should_ignore = (type == MessageType::DagTransaction || type == MessageType::NewActor
                              || type == MessageType::CoinReward);

        if (!should_ignore
            && (messages_->contains(message_id)
                || message_body.init_sender_id == node->account_controller()->system_actor().id())) {
            // eWarning(
            //     "Network Message ignored: already achieved such Request with messageId: {}, from: {}, type: {}",
            //     messageId,
            //     identifier,
            //     type);
            return;
        }

        auto res = messages_->emplace(message_id, std::make_pair(identifier, QDateTime::currentDateTime()));
        if (!res.second) {
            // eWarning(
            //     "Network Message ignored 2: already achieved such Request with messageId: {} from: {}, type:
            //     {}", messageId, identifier, type);
            return;
        } else {
            // eInfo("MessageID emplaced: {}", messageId);
        }
    } else if (status == MessageStatus::Response) {
        auto network_forwarded_messages_locked = *forwarded_messages_;
        auto searchRes                         = network_forwarded_messages_locked->find(message_id);
        if (searchRes != network_forwarded_messages_locked->end()) {
            MessageBody message_edited = message_body;
            message_edited.sender_id   = node->account_controller()->system_actor().id();
            message_edited.nodes_identifiers_to_ignore.emplace(node->node_identifier());

            auto serialized = message_edited.serialize();
            send_message_connections(serialized + std::string(sign),
                                     message_edited,
                                     SendMode::Focused,
                                     searchRes->second.first);
            // eWarning(
            //     "Network Message ignored 3: already achieved such Response with messageId: {} from: {}, type:
            //     {}", messageId, identifier, type);

            return;
        }
    }

    const NetworkPackageStorage package_data(message_body, identifier, std::string(sign));

    Responder responder(this);
    responder.set_message_id(message_id);
    responder.add_identifier(identifier);
    responder.set_message_type(type);
    responder.set_node_id(node_id);
    int luminance = node->luminance_manager()->read_luminance(node_id);
    responder.set_luminance(luminance == -1 ? 1 : luminance);

    if (is_luminance) {
        responder.set_luminance(responder.luminance() * 10); //
    }

#ifdef QT_DEBUG
    if (Network::networkDebug) {
        msgpack::object_handle oh           = msgpack::unpack(serialized.data(), serialized.size());
        msgpack::object        deserialized = oh.get();
        eLog("[Network Message] Received: type {}, status {}, id {}, body: {}",
             type,
             status,
             mess_id,
             (std::stringstream() << deserialized).str());
    }
#endif

    calculate_traffic_->add_bytes_received(ip, message.size());

    if (type == MessageType::DagLightData) {
        eLog("DagLight {}", status);
    }

    // QElapsedTimer timer;
    // timer.start();

    // TODO: not global
    if (send_type == SendMode::Broadcast && type != MessageType::Custom) {
        node->luminance_manager()->increment(node_id);
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
            eWarning("[NetworkManager] {} deserialization failed for custom message", type);
            return;
        }

        if (node->is_custom_app_) {
            emit customMessageReceived(package_data, custom_deserialize_result.value());
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
                    if (identifier != connection->identifier().toStdString()) {
                        if (connection->ip().isEmpty())
                            continue;
                        available_ips.emplace_back(connection->ip().toStdString());
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
                eWarning("[NetworkManager] {} deserialization failed for ips vector in {} state", type, status);
                return;
            }

            auto deserialized_ips_result =
                MessagePack::deserialize_container<std::string>(serialized_ips_result.value());
            if (!deserialized_ips_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for string container in {} state",
                         type,
                         status);
                return;
            }

            for (const auto &ip_address : deserialized_ips_result.value()) {
                bool can_connect        = true;
                auto locked_connections = *connections_;
                for (const auto &existing_connection : *locked_connections) {
                    if (ip_address == existing_connection->ip().toStdString()) {
                        can_connect = false;
                        break;
                    }
                }

                if (can_connect)
                    connect_to_node(QString::fromStdString(ip_address), Network::Protocol::WebSocket);
            }
        }
        break;
    }

    case MessageType::ResponseDfsSize: {
        const auto dfs_size_result = MessagePack::deserialize<DfsP::ResponseDfsSize>(serialized);
        if (!dfs_size_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for response dfs size", type);
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
            eWarning("[NetworkManager] {} deserialization failed for request dfs size", type);
            return;
        }

        node->dfs()->sendSizeReponseMsg(dfs_request_result.value(), responder);
        break;
    }

    case MessageType::NewActor: {
        auto new_actor_result = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
        if (!new_actor_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for new actor", type);
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
                eWarning("[NetworkManager] {} deserialization failed for ActorId in {} state", type, status);
                break;
            }

            node->actor_index()->network_actor_request(actor_id_result.value(), responder);
        } else if (status == MessageStatus::Response) {
            auto actor_result = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
            if (!actor_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for Actor in {} state", type, status);
                break;
            }

            node->actor_index()->save_actor(actor_result.value());
        }

        break;
    }

    case MessageType::Actors: {
        if (status == MessageStatus::Request) {
            auto ignored_actor_id_result = MessagePack::deserialize<std::set<ActorId>>(serialized);
            if (!ignored_actor_id_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for ignored ActorId in {} state",
                         type,
                         status);
                break;
            }

            // node->actorIndex()->network_actors_request(ignored_actor_id_result.value(), responder);
        } else if (status == MessageStatus::Response) {
            auto actors_list_result = MessagePack::deserialize<std::vector<Actor<KeyPublic>>>(serialized);
            if (!actors_list_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for actors vector in {} state", type, status);
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
                eWarning("[NetworkManager] {} deserialization failed in {} state", type, status);
                break;
            }

            node->actor_index()->network_actors_hash_request(actors->first, actors->second, responder);
        } else if (status == MessageStatus::Response) {
            auto actors_list_result = MessagePack::deserialize<std::vector<Actor<KeyPublic>>>(serialized);
            if (!actors_list_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed {} state", type, status);
                break;
            }

            node->actor_index()->network_actors_response(actors_list_result.value());
        }
        break;
    }

    case MessageType::Luminance: {
        if (status == MessageStatus::Request) {
            auto nodes_result = MessagePack::deserialize<std::vector<NodeId>>(serialized);

            if (!nodes_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for Luminance", type);
                break;
            }

            node->luminance_manager()->network_request_luminances(nodes_result.value(), responder);
        } else if (status == MessageStatus::Response) {
            auto luminance_data_result = MessagePack::deserialize<LuminanceData>(serialized);

            if (!luminance_data_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for last modified", type);
                break;
            }

            node->luminance_manager()->network_response_luminances(luminance_data_result.value());
        }

        break;
    }

        // case MessageType::DfsDirData: {
        //     if (status == MessageStatus::Request) {
        //         auto dir_actor_id_result = MessagePack::deserialize<ActorId>(serialized);
        //         if (!dir_actor_id_result.has_value()) {
        //             eWarning("[NetworkManager] {} deserialization failed for ActorId in {} state", type,
        //             status); break;
        //         }
        //         node->dfs()->sendDirData(dir_actor_id_result.value(), 0, messageId);
        //     } else if (status == MessageStatus::Response) {
        //         auto dir_data_result =
        //             MessagePack::deserialize<std::pair<ActorId, std::vector<Dfs::DirRow>>>(serialized);
        //         if (!dir_data_result.has_value()) {
        //             eWarning("[NetworkManager] {} deserialization failed for directory data in {} state",
        //                      type,
        //                      status);
        //             break;
        //         }
        //         const auto &[owner_id, dir_rows] = dir_data_result.value();
        //         node->dfs()->addDirData(owner_id, dir_rows);
        //     }
        //     break;
        // }

    case MessageType::DfsSyncDirs: {
        if (status == MessageStatus::Request) {
            node->dfs()->dirs_manager().network_request_sync(responder);
        } else if (status == MessageStatus::Response) {
            auto last_modified_result = MessagePack::deserialize<std::uint64_t>(serialized);

            if (!last_modified_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for last modified", type);
                break;
            }

            node->dfs()->dirs_manager().network_response_sync(last_modified_result.value(), responder);
        }

        break;
    }

    case MessageType::DfsSyncDirsRows: {
        auto dirs_rows_result =
            MessagePack::deserialize<std::vector<Dfs::Tables::DirsFile::DirsSpace::DirsRow>>(serialized);
        if (!dirs_rows_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for dirs rows", type);
            break;
        }

        node->dfs()->dirs_manager().network_response_from_last_modified(dirs_rows_result.value(), responder);

        break;
    }

    case MessageType::DfsSyncDirRows: {
        if (status == MessageStatus::Request) {
            auto dirs_row_result = MessagePack::deserialize<Dfs::Tables::DirsFile::DirsSpace::DirsRow>(serialized);
            if (!dirs_row_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for dirs row", type);
                return;
            }

            node->dfs()->dirs_manager().network_request_dir_rows(dirs_row_result.value(), responder);
        } else if (status == MessageStatus::Response) {
            auto dirs_row_result =
                MessagePack::deserialize<std::vector<std::pair<ActorId, std::vector<Dfs::DirRow>>>>(serialized);
            if (!dirs_row_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for dir rows", type);
                return;
            }

            node->dfs()->dirs_manager().network_response_dir_rows(dirs_row_result.value(), responder);
        }
        break;
    }

    case MessageType::DfsTempSyncAll: {
        auto res = MessagePack::deserialize<bool>(serialized);
        if (!res.has_value()) {
            break;
        }

        node->dfs()->dirs_manager().network_request_all(responder);
        break;
    }

    case MessageType::DfsStoreFile: {
        auto file_link_result = MessagePack::deserialize<Dfs::FileData>(serialized);
        if (!file_link_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for DirRow", type);
            return;
        }

        file_link_result->dir_row.state = Dfs::FileState::Known;
        node->dfs()->network_store_file(file_link_result->owner_id,
                                        file_link_result->dir_row,
                                        Dfs::NetworkStoreFile::Broadcast);
        send_broadcast_message_further(package_data);

        break;
    }

    case MessageType::DfsFileExistNotification: {
        auto file_state_result = MessagePack::deserialize<Dfs::Packets::FileState>(serialized);
        if (!file_state_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for file state", type);
            break;
        }

        node->dfs()->network_response_file_state(file_state_result.value(), responder);
        break;
    }
    case MessageType::DfsFileFragment: {
        auto fragment_data_result = MessagePack::deserialize<Dfs::Packets::FragmentData>(serialized);
        if (!fragment_data_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for FragmentData", type);
            break;
        }

        // TIMER_START(FRAG)
        node->dfs()->download_manager().file_fragment_achieved(fragment_data_result.value(), identifier);
        // TIMER_END(FRAG)

        break;
    }

    case MessageType::DfsFileState: {
        if (status == MessageStatus::Request) {
            auto link_result = MessagePack::deserialize<Dfs::FileLink>(serialized);
            if (!link_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for request file state", type);
                return;
            }

            node->dfs()->network_request_file_state(link_result->owner_id, link_result->file_id, responder);
        } else if (status == MessageStatus::Response) {
            auto file_state_result = MessagePack::deserialize<Dfs::Packets::FileState>(serialized);
            if (!file_state_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for response file state", type);
                return;
            }

            node->dfs()->network_response_file_state(file_state_result.value(), responder);
        }
        break;
    }

    case MessageType::DfsFileRequest: {
        auto link_result = MessagePack::deserialize<Dfs::FileLinkFragment>(serialized);
        if (!link_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for file request", type);
            return;
        }

        node->dfs()->download_manager().share_stored_file(link_result.value(), responder);

        break;
    }

    case MessageType::DfsFileRequestContinueUpload: {
        auto link_result = MessagePack::deserialize<Dfs::FileLink>(serialized);
        if (!link_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for request file state", type);
            return;
        }

        if (status == MessageStatus::Request)
            node->dfs()->network_request_file_existance(link_result.value(), responder);
        else if (status == MessageStatus::Response)
            node->dfs()->download_manager().add_node_identifier(link_result.value(), identifier);

        break;
    }

    case MessageType::DfsFileRemove: {
        auto file_remove = MessagePack::deserialize<Dfs::Packets::RemoveFile>(serialized);
        if (!file_remove.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for file remove", type);
            return;
        }

        node->dfs()->network_remove_stored_file(file_remove->owner_id,
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
            eWarning("[NetworkManager] {} deserialization failed for collection request", type);
            return;
        }
        const auto &[actor_id, file_id] = db_request_result.value();
        node->dfs()->network_request_collection(actor_id, file_id, responder);

        break;
    }

    case MessageType::DfsCollectionHistory: {
        auto db_history_result =
            MessagePack::deserialize<std::tuple<ActorId, std::string, std::vector<HistoricalCollectionRow>>>(
                serialized);
        if (!db_history_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for collection history", type);
            return;
        }
        const auto &[actor_id, file_id, historical_rows] = db_history_result.value();
        node->dfs()->network_response_historical_collection(actor_id, file_id, historical_rows);
        break;
    }

    case MessageType::DfsCollectionContent: {
        auto db_content_result =
            MessagePack::deserialize<std::tuple<ActorId, std::string, std::vector<DbRow>>>(serialized);
        if (!db_content_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for collection content", type);
            return;
        }
        const auto &[actor_id, file_id, db_rows] = db_content_result.value();
        node->dfs()->network_response_content_collection(actor_id, file_id, db_rows);
        break;
    }

    case MessageType::DfsCollectionRowChange: {
        auto db_add_result =
            MessagePack::deserialize<std::tuple<ActorId, std::string, HistoricalCollectionRow>>(serialized);
        if (!db_add_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for collection change", type);
            return;
        }
        const auto &[actor_id, file_id, historical_row] = db_add_result.value();
        node->dfs()->network_change_collection(actor_id, file_id, historical_row, responder);
        break;
    }

    case MessageType::DfsVectorCreation:
    case MessageType::DfsVectorContent: {
        auto db_content_result = MessagePack::deserialize<Dfs::Packets::DfsVectorContentPackage>(serialized);
        if (!db_content_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for vector content", type);
            return;
        }

        node->dfs()->network_response_content_vector(db_content_result.value());

        if (type == MessageType::DfsVectorCreation) {
            send_broadcast_message_further(package_data);
        }
        break;
    }

    case MessageType::DfsVectorAdd: {
        auto db_content_result = MessagePack::deserialize<Dfs::Packets::VectorRowAdd>(serialized);
        if (!db_content_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for vector add", type);
            return;
        }

        node->dfs()->network_vector_add(db_content_result->owner_id,
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
           (!serialized_messages_result.has_value()) { eWarning("[NetworkManager] {} deserialization failed for
           list of serialized messages in {} state", type, status); break;
                   }
                   auto verify_files_result =
                       MessagePack::deserialize_container<DfsP::VerifyFileMessage>(serialized_messages_result.value());
                   if (!verify_files_result.has_value()) {
                       eWarning("[NetworkManager] {} deserialization failed for list of verify messages in {}
           state", type, status); break;
                   }
                   node->dfs()->verifyFiles(verify_files_result.value(), messageId);
                   break;
               }

                 case MessageStatus::Response: {
                     auto serialized_messages_result =
             MessagePack::deserialize<std::vector<std::string>>(serialized); if
             (!serialized_messages_result.has_value()) { eWarning("[NetworkManager] {} deserialization failed for
             list of serialized messages in {} state", type, status); break;
                     }
                     auto verify_files_result =
                         MessagePack::deserialize_container<DfsP::VerifyFileMessage>(serialized_messages_result.value());
                     if (!verify_files_result.has_value()) {
                         eWarning("[NetworkManager] {} deserialization failed for list of verify messages in {}
             state", type, status); break;
                     }
                     float verify_percent = node->dfs()->percentVerified(verify_files_result.value());
                     break;
                 }
                 }
                 break;
             }

                 case MessageStatus::Response: {
                     auto serialized_messages_result =
             MessagePack::deserialize<std::vector<std::string>>(serialized); if
             (!serialized_messages_result.has_value()) { eWarning("[NetworkManager] {} deserialization failed for
             list of serialized messages in {} state", type, status); break;
                     }
                     auto verify_files_result =
                         MessagePack::deserialize_container<DfsP::VerifyFileMessage>(serialized_messages_result.value());
                     if (!verify_files_result.has_value()) {
                         eWarning("[NetworkManager] {} deserialization failed for list of verify messages in {}
             state", type, status); break;
                     }
                     float verify_percent = node->dfs()->percentVerified(verify_files_result.value());
                     break;
                 }
                 }
                 break;
             }
         */

    case MessageType::DagTransaction: {
        auto transaction_result = MessagePack::deserialize<Transaction>(serialized);
        if (!transaction_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for transaction", type);
            break;
        }

        auto res = node->dag()->network_transaction(transaction_result.value(), responder);

        // if (res.has_value()) {
        send_broadcast_message_further(package_data);
        // }
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
            eWarning("[NetworkManager] {} deserialization failed for transaction result", type);
            break;
        }

        node->dag()->network_transaction_result(transaction_result.value(), responder);
        break;
    }

    case MessageType::DagSections: {
        if (status == MessageStatus::Request) {
            auto range = MessagePack::deserialize<SectionRange>(serialized);
            if (!range.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for dag sync vector", type);
                break;
            }

            auto first = BigNumber::create(range->first);
            auto last  = BigNumber::create(range->last);
            if (!first.has_value() || !last.has_value()) {
                break;
            }

            node->dag()->network_request_sections(first.value(), last.value(), responder);
        } else if (status == MessageStatus::Response) {
            auto txs = MessagePack::deserialize<std::string>(serialized);
            if (!txs.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for dag sync vector", type);
                break;
            }

            node->dag()->network_request_sections_response(txs.value(), responder);
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
                eWarning("[NetworkManager] {} deserialization failed for dag sync vector", type);
                break;
            }

            node->dag()->network_request_light(responder);
        } else if (status == MessageStatus::Response) {
            auto light = MessagePack::deserialize<DagLightPackage>(serialized);
            if (!light.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for dag sync light", type);
                break;
            }

            node->dag()->network_response_light(light.value(), responder);
        }
        break;
    }

    case MessageType::CoinReward: {
        auto reward_request_result = MessagePack::deserialize<Dfs::Reward::RequestReward>(serialized);
        if (!reward_request_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for coin reward", type);
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
        if (!is_luminance) {
            return;
        }
#endif

        if (status == MessageStatus::Request) {
            auto last_info_result = MessagePack::deserialize<bool>(serialized);
            if (!last_info_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for dag sync vector", type);
                break;
            }

            node->dag()->network_status_sync_request(responder);
        } else if (status == MessageStatus::Response) {
            auto last_info_result = MessagePack::deserialize<DagLastInfo>(serialized);
            if (!last_info_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for dag sync vector", type);
                break;
            }

            node->dag()->network_status_sync_response(last_info_result.value(), responder);
        }
        break;
    }

    case MessageType::DagIntervalHash: {
        auto hash_interval = MessagePack::deserialize<HashInterval>(serialized);
        if (!hash_interval.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for hash interval", type);
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
            eWarning("[NetworkManager] {} deserialization failed for dag control", type);
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
            eWarning("[NetworkManager] {} deserialization failed for dag control", type);
            break;
        }

        node->dag()->network_control_range_response(dag_control.value(), responder);
        break;
    }

    default: {
        eCritical("[NetworkManager/messageReceived] Not supported message type: {} ({})",
                  type,
                  std::to_underlying(type));
        break;
    }
    }

    // eLog("Timer: {} ms for {}", timer.elapsed(), type);
}

void NetworkManager::remove_socket_connection() {
    if (QObject::sender() == nullptr)
        return;

    auto connection = qobject_cast<SocketService *>(QObject::sender());

    {
        auto connections_locked = connections();
        auto removed            = connections_locked->erase(connection);
        eLog("[WS] Removed {}", fmt::ptr(connection));
    }
    //    m_reconnections.remove(NetworkReconnect {
    //        .ip = connection->ip(), .port = connection->port(), .protocol = Network::Protocol::WebSocket
    //        });
    if (connection != nullptr) {
        connection->deleteLater();
    }
    check_connections_status();
}

void NetworkManager::socket_error(Network::SocketServiceError error,
                                  QString                     errorData,
                                  std::string                 ip,
                                  std::string                 identifier) {
    // if (QObject::sender() == nullptr) {
    //     return;
    // }

    // auto service = qobject_cast<SocketService *>(QObject::sender());
    eLog("[NetworkManager] Error socket: {} {} {}", error, ip, identifier);

    if (error == Network::SocketServiceError::IncompatibleNetwork
        || error == Network::SocketServiceError::VersionTooOld
        || error == Network::SocketServiceError::VersionTooNew) {
        reconn_.erase(ip);
        failed_ips_.insert(ip);
        emit connectionError(error, QString::fromStdString(ip), QString::fromStdString(identifier), errorData);
        return;
    }

    /*
    if (error != Network::SocketServiceError::DuplicateIdentifier
        && error != Network::SocketServiceError::IncompatibleIdentifier) {
        auto m_reconnectionsToIdentifierLocked = *m_reconnectionsToIdentifier;
        for (auto it = m_reconnectionsToIdentifierLocked->begin(); it != m_reconnectionsToIdentifierLocked->end();
             ++it) {
            if (it->first.ip == ip && it->first.protocol == Network::Protocol::WebSocket) {
                auto r = it->first;
                auto i = it->second;
                QTimer::singleShot(1000, [this, r, i] {
                    this->reconnectSocket(r, i);
                });
                break;
            }
        }
    }
    */
}

void NetworkManager::local_inizialization() {
    eLog("Doesn't find service. Start find local service");
    connect(&network_status_, &NetworkStatus::statusChanged, [this](NetworkStatus::Status status) {
        switch (status) {
        case NetworkStatus::Status::Online:
            eInfo("World network is online");
            break;
        case NetworkStatus::Status::Offline: {
            eInfo("Warning: World network is offline");
            std::set<SocketService *> copied;
            {
                auto connectionsLocked = *connections_;
                copied                 = **connections_;
            }

            for (const auto &connection : copied) {
                connection->flush();
                emit connection->close();
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

    local_ = std::make_shared<QNetworkAddressEntry>(Utils::findLocalIp(Utils::PrintDebug::Off));
    eLog("[NetworkManager] Found local IP: {}", local_->ip().toString());

    if (!local_) {
        eLog("[NetworkManager] Local not found");
        return;
    }

    bool sub = local_->ip().isInSubnet(QHostAddress::parseSubnet("192.168.0.0/16"));
    eLog("Sub: {}", sub);

    if (!sub) {
        // startDiscovery();
        return;
    }

    upnp_dis_ = std::make_unique<UPNPConnection>(local_);
    upnp_net_ = std::make_unique<UPNPConnection>(local_);
    // connect(upnpNet, &UPNPConnection::success, this, &NetworkManager::);
    // connect(upnpDis, &UPNPConnection::success, this, &NetworkManager::startDiscovery);
    connect(upnp_net_.get(), &UPNPConnection::upnpError, [](QString msg) {
        eLog("[NetworkManager] UPnP error: {}", msg);
    });
    connect(upnp_dis_.get(), &UPNPConnection::upnpError, [](QString msg) {
        eLog("[NetworkManager] UPnP Discovery error: {}", msg);
    });
    // eLog("Tunnel creation started!");
    // upnpDis->makeTunnel(extPort, extPort, " UDP ", "Discovery tunnel of ExtraChain ");
    // upnpNet->makeTunnel(tcpPort, tcpPort, "TCP", "Network tunnel of ExtraChain ");

    // UPnP v2
    upnp_connector_ = std::make_unique<UPnPConnector>(local_);
    QObject::connect(upnp_connector_.get(),
                     &UPnPConnector::deviceDiscovered,
                     [&](const QHostAddress &address, const QString &location) {
                         std::cout << "Discovered device at " << address.toString().toStdString()
                                   << " with location: " << location.toStdString() << std::endl;
                         // Now retrieve and parse the device description.
                         upnp_connector_->retrieveDeviceDescription(QUrl(location));
                     });

    QObject::connect(upnp_connector_.get(), &UPnPConnector::errorOccurred, [](const QString &errorMessage) {
        std::cout << "Error: " << errorMessage.toStdString() << std::endl;
    });

    QObject::connect(upnp_connector_.get(), &UPnPConnector::soapResponseReceived, [this](const QString &response) {
        std::cout << "SOAP response: " << response.toStdString() << std::endl;
    });

    QObject::connect(upnp_connector_.get(), &UPnPConnector::controlURLFound, [this](const QString &response) {
        // Example parameters:
        QUrl    controlUrl(response);
        int     internalPort   = 8080;  // The port on your internal application
        int     externalPort   = 8080;  // The external port on your router
        QString protocol       = "TCP"; // Typically TCP
        QString description    = "MyApp Tunnel";
        QString internalClient = local_->ip().toString(); // Your internal IP address

        // Call addPortMapping to establish the tunnel.
        upnp_connector_
            ->addPortMapping(controlUrl, internalPort, externalPort, protocol, description, internalClient);
        // Call getSpecificPortMappingEntry to check if port has been mapped.
        upnp_connector_->getSpecificPortMappingEntry(controlUrl, externalPort, protocol);

        // upnpConnector->removePortMapping(controlUrl, externalPort, protocol);
        // upnpConnector->getSpecificPortMappingEntry(controlUrl, externalPort, protocol);
    });

    // Uncomment to start UPnP connection
    // upnpConnector->discoverDevices();
}

std::string NetworkManager::getNetworkVPNHash() noexcept {
    return network_hash_for_vpn_;
}

void NetworkManager::setNetworkVPNHash() noexcept {
    boost::mt11213b                           rng(std::chrono::system_clock::now().time_since_epoch().count());
    boost::random::uniform_int_distribution<> dist(0, INT_MAX);
    std::string                               salt = Tools::typeToStdStringBytes<int>(dist(rng));

    KeyPrivate key;
    key.generate_random();
    network_hash_for_vpn_ =
        Utils::calculate_hash(ByteArray(key.public_key()).toString()
                                  + node->account_controller()->system_actor().id().to_string() + salt,
                              Utils::HashAlgorithm::Blake3)
            .substr(0, 64);
}

QString NetworkManager::local_ip() {
    return local_->ip().toString();
}

void NetworkManager::initialize_first_node() {
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

                QFile(".first_node").remove();
                return;
            }
        } catch (const std::exception &) {
        }
    }

    save_first_node(first_node_);
}

std::string NetworkManager::first_node() {
    return first_node_;
}

bool NetworkManager::save_first_node(const std::string_view first_node) {
    if (!Utils::is_valid_ip(first_node) && !Utils::is_valid_domain(first_node)) {
        eWarning("[Network] Incorrect first node: {}", first_node);
        return false;
    }

    first_node_ = first_node;

    auto settings       = Utils::read_settings();
    settings.first_node = first_node_;
    bool res            = Utils::write_settings(settings);

    if (!res) {
        eWarning("[Network] First node settings write error");
        return false;
    }

    return true;
}

void NetworkManager::onNewWsConnection() {
    eLog("NetworkManager::onNewWsConnection()");
    auto ws = ws_server_->nextPendingConnection();
    if (ws == nullptr)
        eFatal("[WS] Error: ws == nulltpr");

    bool needToDelete = false;
    if (active_connections_count() >= Network::maxConnections) {
        if (!remove_one_connection()) {
            eLog(
                "[NetworkManager] Can't connect from WS server because the maximum number of "
                "constant connections reached!");
            needToDelete = true;
        }
    }

    auto service = new WebSocketService(ws, node, this, false);
    connectWsService(service);
    if (!needToDelete)
        reconnections_to_identifier_->emplace(NetworkReconnect { .ip       = service->ip(),
                                                                 .port     = service->port(),
                                                                 .protocol = Network::Protocol::WebSocket },
                                              "");
}

bool NetworkManager::remove_one_connection() {
    auto connectionsLocked = *connections_;
    bool isChanged         = false;

    SocketService *doomed;

    for (auto socket : *connectionsLocked) {
        if (!socket->is_constant()) {
            eLog("[NetworkManager] Socket with ip {} was changed to another", socket->ip());

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
        emit doomed->close();
    }

    return isChanged;
}

CalculateTraffic *CalculateTraffic::get_instance() {
    if (calculateTraffic_ == nullptr) {
        calculateTraffic_ = new CalculateTraffic();
    }
    return calculateTraffic_;
}

void CalculateTraffic::add_bytes_sent(const std::string &ip, qint64 bytes) {
    std::unique_lock<std::shared_mutex> lock(m_mutex); // Lock mutex for thread safety
    m_trafficStats[ip].bytesSent += bytes;
}

void CalculateTraffic::add_bytes_received(const std::string &ip, qint64 bytes) {
    std::unique_lock<std::shared_mutex> lock(m_mutex); // Lock mutex for thread safety
    m_trafficStats[ip].bytesReceived += bytes;
}

qint64 CalculateTraffic::total_bytes_sent_from_connection(const std::string &ip) {
    std::shared_lock<std::shared_mutex> lock(m_mutex); // Lock mutex for thread safety
    auto                                it = m_trafficStats.find(ip);
    return (it != m_trafficStats.end()) ? it->second.bytesSent : 0;
}

qint64 CalculateTraffic::total_bytes_received_from_connection(const std::string &ip) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto                                it = m_trafficStats.find(ip);
    return (it != m_trafficStats.end()) ? it->second.bytesReceived : 0;
}

std::pair<std::uint64_t, std::uint64_t> CalculateTraffic::total_bytes() {
    std::shared_lock<std::shared_mutex> lock(m_mutex); // Lock mutex for thread safety
    return std::accumulate(m_trafficStats.begin(),
                           m_trafficStats.end(),
                           std::make_pair(std::uint64_t { 0 }, std::uint64_t { 0 }),
                           [](std::pair<std::uint64_t, std::uint64_t> acc, const auto &connection) {
                               acc.first += connection.second.bytesSent;
                               acc.second += connection.second.bytesReceived;
                               return acc;
                           });
}

QString NetworkManager::found_current_identifier(QString ip, quint16 port) {
    QString res;
    auto    m_reconnectionsToIdentifierLocked = *reconnections_to_identifier_;
    for (auto it = m_reconnectionsToIdentifierLocked->begin(); it != m_reconnectionsToIdentifierLocked->end();
         ++it) {
        if (it->first.ip == ip && it->first.port == port) {
            res = it->second;
            break;
        }
    }
    return res;
}

std::pair<QString, QString> NetworkManager::search_public_ip_and_country_(const QString &ip, bool alt) {
    static QMap<QString, QString> cache;
    if (!ip.isEmpty() && cache.contains(ip)) {
        return { ip, cache[ip] };
    }

    try {
        QString query = alt ? "https://freeipapi.com/api/json" : "http://ip-api.com/json";
        if (!ip.isEmpty()) {
            query += "/" + ip;
        }

        QUrl                  url(query);
        QNetworkAccessManager manager;
        QNetworkRequest       request(url);
#ifdef IS_APP_UI_CLIENT
        request.setTransferTimeout(4000);
#else
        request.setTransferTimeout(5000);
#endif
        QNetworkReply *reply = manager.get(request);

        QString    ip, country, output;
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

        QString errorText;
        QObject::connect(reply, &QNetworkReply::finished, [&]() {
            if (reply->error() != QNetworkReply::NoError) {
                errorText = reply->errorString();
                return;
            }
            output = reply->readAll();
        });
        loop.exec();
        reply->deleteLater();

        if (!errorText.isEmpty())
            throw std::runtime_error(errorText.toStdString());

        QJsonParseError parseError;
        QJsonDocument   jsonDoc = QJsonDocument::fromJson(output.toUtf8(), &parseError);

        if (parseError.error != QJsonParseError::NoError)
            throw std::runtime_error("Failed to parse JSON:" + parseError.errorString().toStdString());
        if (!jsonDoc.isObject())
            throw std::runtime_error("JSON is not an object");

        QJsonObject jsonObj = jsonDoc.object();

        ip      = jsonObj.value(alt ? "ipAddress" : "query").toString();
        country = jsonObj.value(alt ? "countryName" : "country").toString();

        if (country.contains("United Kingdom")) {
            country = "United Kingdom";
        }

        if (country == "United States of America") {
            country = "United States";
        }

        if (country == "The Netherlands") {
            country = "Netherlands";
        }

        eLog("Country: {}", country);
        cache.insert(ip, country);
        return { ip, country };
    } catch (const std::exception &error) {
        eCritical("Get public ip error: {}", error.what());

        if (!alt) {
            return search_public_ip_and_country_(ip, true);
        }

#ifdef Q_OS_LINUX
        return {};
#endif

        return { ip.isEmpty() ? public_ip_.c_str() : ip, "Security" };
    } catch (...) {
        eCritical("Get public ip error unknown");

        if (!alt) {
            return search_public_ip_and_country_(ip, true);
        }

#ifdef Q_OS_LINUX
        return {};
#endif

        return { ip.isEmpty() ? public_ip_.c_str() : ip, "Security" };
    }
}

NetworkPackageStorage::NetworkPackageStorage(const MessageBody &body,
                                             const std::string &identifier,
                                             const std::string &signature)
    : msg_body(body)
    , prev_identifier(identifier)
    , sign(signature) {
}

std::string Responder::send_response_impl(const std::string &data_serialized,
                                          MessageType        type,
                                          SendMode           send_mode,
                                          MessageStatus      status) const {
    bool check = network_manager->send_message_checker(type, send_mode, status, *this);
    if (!check) {
        return "";
    }

    auto message_id = network_manager->send_message_send(data_serialized, type, send_mode, status, *this);
    return message_id;
}
