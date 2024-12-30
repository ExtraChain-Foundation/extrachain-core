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

#include "blockchain/blockchain.h"
#include "blockchain/actor_index.h"
#include "dfs/dfs_controller.h"
#include "managers/connections_manager.h"
#include "managers/data_mining_manager.h"
#include "managers/extrachain_node.h"
#include "managers/transaction_manager.h"
#include "network/upnpconnection.h"
#include "network/websocket_service.h"
#include "utils/exc_logs.h"
#include "dfs/historical_collection.h"

#include <filesystem>
#include <fstream>
#include <vector>

CalculateTraffic *CalculateTraffic::calculateTraffic_ = nullptr;

SafePtr<std::set<SocketService *>> NetworkManager::connections() const {
    return m_connections;
}

bool NetworkManager::serverStatus(Network::Protocol protocol) const {
    switch (protocol) {
    case Network::Protocol::Udp:
        break;
    case Network::Protocol::WebSocket:
        return wsServer == nullptr ? false : wsServer->isListening();
    case Network::Protocol::Undefined:
        return false;
    }
    return false;
}

SafePtr<std::map<NetworkReconnect, QString>> NetworkManager::reconnections() {
    return m_reconnectionsToIdentifier;
}
CalculateTraffic *NetworkManager::getCalculateTraffic() const {
    return calculateTraffic;
}

void NetworkManager::subscribeCustom(const ActorId &actorId) {
    this->m_customPool.insert(actorId);
}

void NetworkManager::unsubscribeCustom(const ActorId &actorId) {
    this->m_customPool.erase(actorId);
}

bool NetworkManager::message_pause() const {
    return message_pause_;
}

void NetworkManager::set_message_pause(bool newMessage_pause) {
    message_pause_ = newMessage_pause;

    if (!message_pause_) {
        readFromPauseCache();
    }
}

NetworkManager::NetworkManager(ExtraChainNode *node)
    : QObject(node)
    , node(node) {
    localInizialization();
    m_reconnectTimer = new QTimer(this);
    calculateTraffic = CalculateTraffic::GetInstance();
}

void NetworkManager::process() {
    if (!node->isClientApp())
        return;
    connect(m_reconnectTimer, &QTimer::timeout, this, &NetworkManager::reconnection);
    m_reconnectTimer->start(Utils::RECONNECT_INTERVAL);
}

void NetworkManager::reconnection() {
    eLog("Count reconnections: {}", m_reconnectionsToIdentifier->size());
    auto m_reconnectionsToIdentifierLocked = *m_reconnectionsToIdentifier;
    for (auto it = m_reconnectionsToIdentifierLocked->begin(); it != m_reconnectionsToIdentifierLocked->end();
         ++it)
        connectToWebSocket(it->first.ip, it->first.port);
}

void NetworkManager::reconnectSocket(const NetworkReconnect &connectInfo, QString identifier) {
    eLog("Reconnect socket: {} {}", connectInfo.ip, connectInfo.port);
    auto connectionsLocked = *m_connections;
    for (auto it = connectionsLocked->begin(); it != m_connections->end(); ++it) {
        if ((*it)->identifier() == identifier) {
            emit(*it)->close();
            emit(*it)->finished();
        }
        break;
    }

    connectToWebSocket(connectInfo.ip, connectInfo.port);
}

void NetworkManager::setupProxy(QNetworkProxy::ProxyType type,
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
    connect(service, &WebSocketService::error, this, &NetworkManager::socketError);
    connect(service, &WebSocketService::disconnected, this, &NetworkManager::removeWsConnection);
    connect(service, &WebSocketService::activated, this, &NetworkManager::checkConnectionsStatus);
    connect(service, &WebSocketService::activated, this, [&] {
        auto senderObj = QObject::sender();
        if (senderObj == nullptr)
            return;

        auto service = qobject_cast<SocketService *>(senderObj);

        emit this->newSocketActivated();
        emit this->newSocketActivatedWithParams(service->ip().toStdString(), service->identifier().toStdString());
    });

    {
        auto connectionsLocked = *m_connections;
        if (!connectionsLocked->contains(service))
            connectionsLocked->insert(service);
    }
    connect(service, &WebSocketService::shareConnections, this, [&](const QJsonArray connectionsArr) {
        eLog("shareConnections {}", QJsonDocument(connectionsArr).toJson(QJsonDocument::Compact));
        auto initIP = node->getInitPublicIPAndCountry().first;

        if (m_connections->size() >= Network::maxConnections) {
            eLog("shareConnections ignored by max connections limit");
            return;
        }

        for (const QJsonValue &value : connectionsArr) {
            bool canConnect = true;
            auto ip         = value.toString();
            {
                auto connectionsLocked = *m_connections;
                for (const auto &connItem : *connectionsLocked) {
                    if (ip == connItem->ip() || ip == initIP) {
                        canConnect = false;
                        break;
                    }
                }
            }

            if (canConnect)
                connectToNode(ip, Network::Protocol::WebSocket);
        }
    });
}

void NetworkManager::removeConnection(const QString &identifier) {
    if (identifier.isEmpty())
        eFatal("Try remove with empty identifier");
    auto connectionsLocked = *m_connections;
    for (const auto &connection : *connectionsLocked) {
        if (connection->identifier() == identifier)
            emit connection->close();
    }
}

NetworkManager::~NetworkManager() {
    eLog("[NetworkManager] Finish him with {} connections", m_connections->size());

    auto connectionsLocked = *m_connections;
    for (const auto &connection : *connectionsLocked) {
        connection->final();
        emit connection->close();
        // emit connection->finished();
    }
    connectionsLocked->clear();
}

void NetworkManager::checkConnectionsStatus() {
    m_reconnectTimer->stop();
    bool flag  = false;
    int  count = 0;
    {
        auto connectionsLocked = *m_connections;
        std::for_each(connectionsLocked->begin(), connectionsLocked->end(), [&](SocketService *el) {
            flag = flag || el->isActive();
            if (el->isActive()) {
                count++;
            }
        });
    }
    emit connectionStatusChanged(flag);
    emit connectionsCountChanged(count); // TODO: check prev count value

    if (flag) { // TODO: replace to networkStatusChanged slot
        sendFromCache();
    }
}

void NetworkManager::startNetwork() {
    eLog("[NetworkManager] Start servers... {}", (wsPort == 2222 ? "Network" : "DFS"));

    if (!local) {
        eLog("[NetworkManager] Can't detect local ip");
        return;
    }

    if (!Network::isStartedServer)
        return;
    wsServer = new QWebSocketServer("ExtraChain", QWebSocketServer::SslMode::NonSecureMode);

    if (!wsServer->listen(QHostAddress::Any, wsPort)) {
        eLog("[NetworkManager] Can't listen port");
        return;
    }

    connect(wsServer, &QWebSocketServer::newConnection, this, &NetworkManager::onNewWsConnection);
    connect(wsServer, &QWebSocketServer::serverError, [](QWebSocketProtocol::CloseCode closeCode) {
        eLog("[WS] Server error code: {}", int(closeCode));
    });
    connect(wsServer, &QWebSocketServer::closed, [] {
        eLog("[WS] Server: closed");
    });
    connect(wsServer, &QWebSocketServer::acceptError, [](QAbstractSocket::SocketError socketError) {
        eLog("[WS] Server socker error: {}", int(socketError));
    });

    eLog("[WS] Start listening: {}:{}",
         wsServer->serverAddress(),
         wsServer->serverPort()); // << wsServer->serverName();
}

[[maybe_unused]] void NetworkManager::startDiscovery() {
    eLog("NetworkManager::startDiscovery()");
    // discoveryService = new DiscoveryService(extPort, tcpPort, local);
    // ThreadPool::addThread(discoveryService);
    // connect(discoveryService, &DiscoveryService::ClientDiscovered, this,
    // &NetworkManager::addConnectionFromPair);
}

void NetworkManager::connectToNode(const QString    &ip,
                                   Network::Protocol protocol,
                                   const bool        request,
                                   const bool        isConstant) {
    if (m_connections->size() >= Network::maxConnections) {
        if (!removeOneConnection()) {
            eLog("[NetworkManager] Can't connect because the maximum number of connections");
            return;
        }
    }

    if (ip.isEmpty())
        return;

    const quint16 port = (protocol == Network::Protocol::WebSocket ? wsPort : 0);
    eLog("[NetworkManager] Connect to {}, protocol: {}, port: {}", ip, Utils::enum_value_name(protocol), port);
    // m_reconnections.insert(NetworkReconnect { .ip = ip, .port = port, .protocol = protocol });

    using Network::Protocol;
    switch (protocol) {
    case Protocol::Udp:
        break;
    case Protocol::WebSocket:
        connectToWebSocket(ip.simplified(), port, request, isConstant);
        break;
    case Protocol::Undefined:
        eFatal("Undefined connectToNode");
    }
}

void NetworkManager::connectToWebSocket(const QString &ip,
                                        quint16        port,
                                        bool           requestListNodes,
                                        const bool     isConstant) {
    auto service = new WebSocketService(nullptr, node, this, isConstant);
    service->open(ip, port);
    connectWsService(service, requestListNodes);
    m_reconnectionsToIdentifier
        ->emplace(NetworkReconnect { .ip = ip, .port = port, .protocol = Network::Protocol::WebSocket }, "");
}

void NetworkManager::sendMessage(const std::string    &serialized_message,
                                 Config::Net::TypeSend type_send,
                                 const std::string    &receiver_identifier,
                                 MessageType           type_info,
                                 MessageStatus         status_info) {
    if (!isActiveConnectionExists()) {
        eLog("[NetworkManager] Save message to cache {} {}", type_info, status_info);
        saveToCache(serialized_message, type_send, receiver_identifier);
        return;
    }

    static auto isSendCheck = [](const Config::Net::TypeSend &type_send,
                                 const std::string           &receiver_identifier,
                                 const std::string           &socket_identifier) {
        switch (type_send) {
        case Config::Net::TypeSend::Except:
            return socket_identifier != receiver_identifier;
        case Config::Net::TypeSend::Focused:
            return socket_identifier == receiver_identifier;
        case Config::Net::TypeSend::All:
            return true;
        default:
            return false;
        }
    };

    auto connectionsLocked = *m_connections;
    for (const auto &service : *connectionsLocked) {
        if (service->isActive()
            && isSendCheck(type_send, receiver_identifier, service->identifier().toStdString())) {
            calculateTraffic->addBytesSent(service->ip().toStdString(), serialized_message.size());

            SocketService::Priority priority = type_info == MessageType::DfsAddSegment
                                                   ? SocketService::Priority::Low
                                                   : SocketService::Priority::Normal;
            if (type_info == MessageType::Custom || type_info == MessageType::BlockchainNewBlock) {
                priority = SocketService::Priority::High;
            }

            service->sendMessageQuality(QByteArray::fromStdString(serialized_message), priority);
        }
    }
}

void NetworkManager::saveCustomMessage(const std::string &messageId, const std::string &identifier) {
    m_receivedMessageId->insert_or_assign(messageId, std::make_pair(identifier, true));
}

void NetworkManager::sendCustomMessageFurther(const CustomMessage &customMessage,
                                              const MessageStatus &status,
                                              const std::string   &messageId,
                                              const std::string   &identifier) {
    auto receivedMessageIdLocked = *m_receivedMessageId;
    auto it                      = receivedMessageIdLocked->find(messageId);
    if (it != receivedMessageIdLocked->end() && !it->second.second) {
        node->network()->send_message(customMessage,
                                      MessageType::Custom,
                                      status,
                                      messageId,
                                      Config::Net::TypeSend::Except);

        it->second.first  = identifier;
        it->second.second = true;
    }
}

void NetworkManager::saveToCache(const std::string    &serialized_message,
                                 Config::Net::TypeSend typeSend,
                                 const std::string    &receiver_identifier) {
    std::ofstream file;
    file.open(NetworkCacheFile, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
    if (!file.is_open()) {
        eFatal("[NetworkManager/saveToCache] Error open cache file");
    }
    auto size             = std::filesystem::file_size(NetworkCacheFile);
    auto typeSendToString = [=](Config::Net::TypeSend ts) -> std::string {
        switch (ts) {
        case Config::Net::TypeSend::All:
            return "All";
        case Config::Net::TypeSend::Except:
            return "Except";
        case Config::Net::TypeSend::Focused:
            return "Focused";
        }
        return "";
    };

    if (size == 0) {
        std::string serializedMessage = Serialization::serialize(
            std::vector<std::string> { serialized_message, typeSendToString(typeSend), receiver_identifier });
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

        std::vector<std::string> list              = Serialization::deserialize(data);
        std::string              serializedMessage = Serialization::serialize(
            std::vector<std::string> { serialized_message, typeSendToString(typeSend), receiver_identifier });
        list.push_back(serializedMessage);
        file.close();
        file.open(NetworkCacheFile, std::ofstream::out | std::ofstream::trunc);
        file << Serialization::serialize(list);
        file.close();
    }
}

void NetworkManager::sendFromCache() {
    eLog("[NetworkManager] Load from cache");

    QFile file(QString::fromStdString(NetworkCacheFile));
    if (!file.exists() || !file.open(QFile::ReadOnly)) {
        return;
    }

    std::vector<std::string> allPackages = Serialization::deserialize(file.readAll().toStdString());
    file.close();
    file.remove();

    auto typeSendFromString = [=](std::string typeSendStr) -> Config::Net::TypeSend {
        if (typeSendStr == "All")
            return Config::Net::TypeSend::All;
        else if (typeSendStr == "Except")
            return Config::Net::TypeSend::Except;
        else if (typeSendStr == "Focused")
            return Config::Net::TypeSend::Focused;
        return Config::Net::TypeSend::All;
    };

    for (int numberPackage = 0; numberPackage < allPackages.size(); numberPackage++) {
        const std::vector<std::string> deserializedList = Serialization::deserialize(allPackages[numberPackage]);
        if (deserializedList.size() < 3) {
            eWarning("Size deserialized data in not correct");
            continue;
        }
        const std::string           deserialized_message = deserializedList[0];
        const Config::Net::TypeSend typeSend             = typeSendFromString(deserializedList[1]);
        const std::string           receiver_identifier  = deserializedList[2];
        sendMessage(deserialized_message,
                    typeSend,
                    receiver_identifier,
                    MessageType::Unknown,
                    MessageStatus::NoStatus);
    }
}

void NetworkManager::saveToPauseCache(const std::string &message,
                                      const std::string &ip,
                                      const std::string &identifier) {
    std::ofstream file;
    file.open(MessagePauseCacheFile, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
    if (!file.is_open()) {
        eFatal("[NetworkManager/saveToCache] Error open cache file");
    }
    auto size = std::filesystem::file_size(MessagePauseCacheFile);

    if (size == 0) {
        std::string serializedMessage =
            Serialization::serialize(std::vector<std::string> { message, ip, identifier });
        file << Serialization::serialize(std::vector<std::string> { serializedMessage });
        file.flush();
        file.close();
    } else {
        std::ifstream inputFile;
        inputFile.open(MessagePauseCacheFile, std::ios::binary);
        std::string data;
        if (inputFile.is_open()) {
            inputFile >> data;
        }
        inputFile.close();

        std::vector<std::string> list = Serialization::deserialize(data);
        std::string              serializedMessage =
            Serialization::serialize(std::vector<std::string> { message, ip, identifier });
        list.push_back(serializedMessage);
        file.close();
        file.open(MessagePauseCacheFile, std::ofstream::out | std::ofstream::trunc);
        file << Serialization::serialize(list);
        file.close();
    }
}

void NetworkManager::readFromPauseCache() {
    eLog("[NetworkManager] Load from message pause cache");

    QFile file(QString::fromStdString(MessagePauseCacheFile));
    if (!file.exists() || !file.open(QFile::ReadOnly)) {
        return;
    }

    std::vector<std::string> allPackages = Serialization::deserialize(file.readAll().toStdString());
    file.close();
    file.remove();

    for (int numberPackage = 0; numberPackage < allPackages.size(); numberPackage++) {
        const std::vector<std::string> deserializedList = Serialization::deserialize(allPackages[numberPackage]);
        if (deserializedList.size() < 3) {
            eWarning("Size deserialized data in not correct");
            continue;
        }
        const std::string deserialized_message = deserializedList[0];
        const std::string ip                   = deserializedList[1];
        const std::string identifier           = deserializedList[2];
        messageReceived(deserialized_message, ip, identifier);
    }
}

bool NetworkManager::isActiveConnectionExists() {
    auto connectionsLocked = *m_connections;
    if (connectionsLocked->empty())
        return false;

    for (const auto &el : *connectionsLocked) {
        if (el->isActive())
            return true;
    }

    return false;
}

bool NetworkManager::checkMsgCount(const std::string &msg) {
    bool                             flag_result = true;
    bool                             value       = 0;
    std::string                      hashMsg     = Utils::calculate_hash(msg);
    QMap<std::string, int>::iterator it          = msgHashList.find(hashMsg);

    if (it == msgHashList.end())
        msgHashList.insert(hashMsg, value);
    else {
        if (msgHashList.find(hashMsg).value() == m_connections->size() - 1) {
            msgHashList.remove(hashMsg);
            flag_result = false;
        } else {
            msgHashList.find(hashMsg).value()++;
            flag_result = true;
        }
    }

    return flag_result;
}

void NetworkManager::messageReceived(const std::string &message,
                                     const std::string &ip,
                                     const std::string &identifier) {
    if (!checkMsgCount(message)) {
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

    MessageBody   message_body = message_body_expected.value();
    MessageType   type         = message_body.message_type;
    MessageStatus status       = message_body.status;
    std::string   serialized   = message_body.data;
    std::string   messId       = message_body.message_id;
    std::string   messageId(messId.begin(), messId.end());

    if (status == MessageStatus::Request) {
        m_messages[messageId] = identifier;
    }

#ifdef QT_DEBUG
    if (Network::networkDebug) {
        msgpack::object_handle oh           = msgpack::unpack(serialized.data(), serialized.size());
        msgpack::object        deserialized = oh.get();
        eLog("[Network Message] Received: type {}, status {}, id {}, body: {}",
             type,
             status,
             messId,
             (std::stringstream() << deserialized).str());
    }
#endif

    calculateTraffic->addBytesReceived(ip, message.size());

    if (type != MessageType::Custom && message_pause_) {
        saveToPauseCache(message, ip, identifier);
        return;
    }

    // try {
    switch (type) {
    case MessageType::Custom: {
        eSuccess("Achieved Custom package. MessageID: {} | SenderId: {} | Status: {}",
                 messageId,
                 message_body.sender_id,
                 magic_enum::enum_name(status));

        auto received_msg_id_locked = *m_receivedMessageId;
        auto emplace_result         = received_msg_id_locked->try_emplace(messageId, std::make_pair("", false));

        const auto custom_deserialize_result = MessagePack::deserialize<CustomMessage>(serialized);

        // if (custom_deserialize_result.has_value() && status == MessageStatus::Response && ) {

        // }
        if (!emplace_result.second) {
            if (emplace_result.first->second.second && status == MessageStatus::Response) {
                auto msg_identifier = emplace_result.first->second.first;

                eSuccess("Custom Response package forwarded further {} {}", messageId, msg_identifier);

                auto        main_actor = node->accountController()->mainActor();
                MessageBody outgoing_message =
                    make_message(serialized, MessageType::Custom, status, main_actor->id(), messageId);
                auto serialized_message = outgoing_message.serialize();
                auto signature          = ByteArray(main_actor->key().sign(serialized_message)).toString();
                sendMessage(serialized_message + signature, Config::Net::TypeSend::Focused, msg_identifier);
            }
            // return;
        }

        if (!custom_deserialize_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for custom message", type);
            return;
        }
        const auto custom_message   = custom_deserialize_result.value();
        const bool is_owner_in_pool = m_customPool.contains(custom_message.owner);

        if (/*is_owner_in_pool*/ true)
            emit customMessageReceived(custom_message, status, messageId, message_body.sender_id, identifier);
        else
            sendCustomMessageFurther(custom_message, status, messageId, identifier);

        break;
    }

    case MessageType::ShareConnections: {
        if (status == MessageStatus::Request) {
            eInfo("Achieved ShareConnections(Request) {}", messageId);
            std::vector<std::string> available_ips;

            {
                auto locked_connections = *m_connections;
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
                                              MessageStatus::Response,
                                              messageId,
                                              Config::Net::TypeSend::Focused);
            }
        } else if (status == MessageStatus::Response) {
            eInfo("Achieved ShareConnections(Response) {}", messageId);
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
                auto locked_connections = *m_connections;
                for (const auto &existing_connection : *locked_connections) {
                    if (ip_address == existing_connection->ip().toStdString()) {
                        can_connect = false;
                        break;
                    }
                }

                if (can_connect)
                    connectToNode(QString::fromStdString(ip_address), Network::Protocol::WebSocket);
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
        node->dfs()->sendSizeReponseMsg(dfs_request_result.value(), messageId);
        break;
    }

    case MessageType::ResponseBlockCount: {
        const auto block_count_result = MessagePack::deserialize<DfsP::ResponseBlockCount>(serialized);
        if (!block_count_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for response block count", type);
            return;
        }

        BigNumber count = block_count_result.value().blockCount;
        emit      messageCountReceived(count);
        break;
    }

    case MessageType::RequestBlockCount: {
        const auto block_request_result = MessagePack::deserialize<DfsP::RequestBlockCount>(serialized);
        if (!block_request_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for request block count", type);
            return;
        }
        BigNumber dfs_block_count = node->blockchain()->getBlockCount();
        node->dfs()->sendCountReponseMsg(block_request_result.value(), messageId, dfs_block_count);
        break;
    }

    case MessageType::NewActor: {
        auto new_actor_result = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
        if (!new_actor_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for new actor", type);
            return;
        }
        auto actor_handling_result = node->actorIndex()->handleNewActor(new_actor_result.value());
        if (actor_handling_result == Errors::FILE_NOT_EXISTS) {
            emit accrual(new_actor_result.value().id());
        }
        break;
    }

    case MessageType::Actor: {
        if (status == MessageStatus::Request) {
            auto actor_id_result = MessagePack::deserialize<ActorId>(serialized);
            if (!actor_id_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for ActorId in {} state", type, status);
                break;
            }
            node->actorIndex()->handleGetActor(actor_id_result.value(), messageId);
        } else if (status == MessageStatus::Response) {
            auto actor_result = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
            if (!actor_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for Actor in {} state", type, status);
                break;
            }
            node->actorIndex()->handleNewActor(actor_result.value());
        }
        break;
    }

    case MessageType::ActorAll: {
        if (status == MessageStatus::Request) {
            auto ignored_actor_id_result = MessagePack::deserialize<ActorId>(serialized);
            if (!ignored_actor_id_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for ignored ActorId in {} state",
                         type,
                         status);
                break;
            }
            node->actorIndex()->handleGetAllActor(ignored_actor_id_result.value(), messageId);
        } else if (status == MessageStatus::Response) {
            auto actors_list_result = MessagePack::deserialize<std::vector<ActorId>>(serialized);
            if (!actors_list_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for actors vector in {} state", type, status);
                break;
            }
            node->actorIndex()->handleNewAllActors(actors_list_result.value());
        }
        break;
    }

    case MessageType::ActorCount:
        break;

    case MessageType::DfsDirData: {
        if (status == MessageStatus::Request) {
            auto dir_actor_id_result = MessagePack::deserialize<ActorId>(serialized);
            if (!dir_actor_id_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for ActorId in {} state", type, status);
                break;
            }
            node->dfs()->sendDirData(dir_actor_id_result.value(), 0, messageId);
        } else if (status == MessageStatus::Response) {
            auto dir_data_result =
                MessagePack::deserialize<std::pair<ActorId, std::vector<Dfs::DirRow>>>(serialized);
            if (!dir_data_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for directory data in {} state",
                         type,
                         status);
                break;
            }
            const auto &[owner_id, dir_rows] = dir_data_result.value();
            node->dfs()->addDirData(owner_id, dir_rows);
        }
        break;
    }

    case MessageType::DfsLastModified: {
        auto last_modified_result = MessagePack::deserialize<std::uint64_t>(serialized);
        if (!last_modified_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for last modified", type);
            break;
        }
        node->dfs()->sendSync(last_modified_result.value(), messageId);
        break;
    }

    case MessageType::DfsAddFile: {
        auto dfs_add_result = MessagePack::deserialize<std::pair<ActorId, Dfs::DirRow>>(serialized);
        if (!dfs_add_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for DirRow", type);
            break;
        }
        node->dfs()->network_add_file(dfs_add_result->first, dfs_add_result->second, true);
        break;
    }

    case MessageType::DfsRequestFile: {
        auto file_request_result = MessagePack::deserialize<std::pair<ActorId, std::string>>(serialized);
        if (!file_request_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for file request", type);
            break;
        }
        const auto &[requesting_actor_id, requested_file_name] = file_request_result.value();
        node->dfs()->sendFile(requesting_actor_id, requested_file_name, messageId);
        break;
    }

    case MessageType::DfsRequestFileSegment: {
        auto segment_request_result = MessagePack::deserialize<DfsP::RequestFileSegmentMessage>(serialized);
        if (!segment_request_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for file segment request", type);
            break;
        }
        emit fetchFragment(segment_request_result.value(), messageId);
        break;
    }

    case MessageType::DfsAddSegment: {
        auto segment_add_result = MessagePack::deserialize<DfsP::SegmentMessage>(serialized);
        if (!segment_add_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for segment message", type);
            break;
        }
        emit addFragSignal(segment_add_result.value());
        break;
    }

    case MessageType::DfsEditSegment: {
        auto segment_edit_result = MessagePack::deserialize<DfsP::SegmentMessage>(serialized);
        if (!segment_edit_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for edit segment", type);
            break;
        }
        node->dfs()->insertFragment(segment_edit_result.value());
        break;
    }

    case MessageType::DfsDeleteSegment: {
        auto segment_delete_result = MessagePack::deserialize<DfsP::DeleteSegmentMessage>(serialized);
        if (!segment_delete_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for delete segment", type);
            break;
        }
        node->dfs()->deleteFragment(segment_delete_result.value());
        break;
    }

    case MessageType::DfsRemoveFile: {
        auto file_remove_result = MessagePack::deserialize<DfsP::RemoveFileMessage>(serialized);
        if (!file_remove_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for remove file", type);
            break;
        }
        node->dfs()->removeFile(file_remove_result.value());
        break;
    }

    case MessageType::DfsSendingFileDone: {
        auto file_done_result = MessagePack::deserialize<std::pair<ActorId, std::string>>(serialized);
        if (!file_done_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for sending file done", type);
            break;
        }
        const auto &[sender_id, file_hash] = file_done_result.value();
        eLog("[Dfs] File done: {} {}", sender_id, file_hash);
        break;
    }

    case MessageType::DfsCollectionRequest: {
        auto db_request_result = MessagePack::deserialize<std::pair<ActorId, std::string>>(serialized);
        if (!db_request_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for collection request", type);
            break;
        }
        const auto &[requester_id, requested_file_id] = db_request_result.value();
        node->dfs()->network_request_collection(requester_id, requested_file_id, messageId);
        break;
    }

    case MessageType::DfsCollectionHistory: {
        auto db_history_result =
            MessagePack::deserialize<std::tuple<ActorId, std::string, std::vector<HistoricalCollectionRow>>>(
                serialized);
        if (!db_history_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for collection history", type);
            break;
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
            break;
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
            break;
        }
        const auto &[actor_id, file_id, historical_row] = db_add_result.value();
        node->dfs()->network_change_collection(actor_id, file_id, historical_row);
        break;
    }

    case MessageType::DfsVerifyList: {
        switch (status) {
        case MessageStatus::NoStatus:
            break;
        case MessageStatus::Request: {
            auto serialized_messages_result = MessagePack::deserialize<std::vector<std::string>>(serialized);
            if (!serialized_messages_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for list of serialized messages in {} state",
                         type,
                         status);
                break;
            }
            auto verify_files_result =
                MessagePack::deserialize_container<DfsP::VerifyFileMessage>(serialized_messages_result.value());
            if (!verify_files_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for list of verify messages in {} state",
                         type,
                         status);
                break;
            }
            node->dfs()->verifyFiles(verify_files_result.value(), messageId);
            break;
        }
        case MessageStatus::Response: {
            auto serialized_messages_result = MessagePack::deserialize<std::vector<std::string>>(serialized);
            if (!serialized_messages_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for list of serialized messages in {} state",
                         type,
                         status);
                break;
            }
            auto verify_files_result =
                MessagePack::deserialize_container<DfsP::VerifyFileMessage>(serialized_messages_result.value());
            if (!verify_files_result.has_value()) {
                eWarning("[NetworkManager] {} deserialization failed for list of verify messages in {} state",
                         type,
                         status);
                break;
            }
            float verify_percent = node->dfs()->percentVerified(verify_files_result.value());
            break;
        }
        }
        break;
    }

    case MessageType::BlockchainGenesisBlock: {
        auto genesis_block_result = MessagePack::deserialize<GenesisBlock>(serialized);
        if (!genesis_block_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for genesis block", type);
            break;
        }
        if (!genesis_block_result.value().isEmpty()) {
            auto block_variant = BlockVariant(genesis_block_result.value());
            node->blockchain()->addBlockFromNetwork(block_variant, messageId);
        } else {
            eLog("false genesis block");
        }
        break;
    }

    case MessageType::BlockchainNewBlock: {
        auto new_block_result = MessagePack::deserialize<Block>(serialized);
        if (!new_block_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for new block", type);
            break;
        }
        if (!new_block_result.value().isEmpty()) {
            auto block_variant = BlockVariant(new_block_result.value());
            node->blockchain()->addBlockFromNetwork(block_variant, messageId);
        }
        break;
    }

    case MessageType::BlockchainTransaction: {
        eLog("BlockchainTransaction");
        auto transaction_result = MessagePack::deserialize<Transaction>(serialized);
        if (!transaction_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for transaction", type);
            break;
        }
        node->transactionManager()->addTransaction(transaction_result.value());
        break;
    }

    case MessageType::BlockchainRequestBlock: {
        eLog("BlockchainRequestBlock");
        auto block_request_result = MessagePack::deserialize<std::pair<BlockType, BigNumber>>(serialized);
        if (!block_request_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for block request", type);
            break;
        }
        const auto &[block_type, block_number] = block_request_result.value();
        if (block_type == BlockType::Data)
            node->blockchain()->sendBlockByNumber(block_number);
        else if (block_type == BlockType::Genesis)
            node->blockchain()->sendLastGenesisBlock();
        break;
    }

    case MessageType::BlockchainSync: {
        auto sync_from_block_result = MessagePack::deserialize<BigNumber>(serialized);
        if (!sync_from_block_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for blockchain sync", type);
            break;
        }
        node->blockchain()->syncResponseFromNetwork(sync_from_block_result.value(), messageId);
        break;
    }

    case MessageType::BlockchainAnarchy: {
        eLog("! BlockchainAnarchy !");
        break;
    }

    case MessageType::FragmentDataInfo: {
        auto fragment_info_result = MessagePack::deserialize<DfsF::FragmentsInfo>(serialized);
        if (!fragment_info_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for fragment info", type);
            break;
        }
        fragment_info_result.value().print();
        break;
    }

    case MessageType::FragmentsDataListInfo: {
        auto fragments_list_result = MessagePack::deserialize<std::vector<DfsF::FragmentsInfo>>(serialized);
        if (!fragments_list_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for fragments info list", type);
            break;
        }
        eLog("Recieved fragment data info from list");
        for (const auto &fragment_info : fragments_list_result.value()) {
            fragment_info.print();
        }
        break;
    }

    case MessageType::BlockchainCoinReward: {
        auto reward_request_result = MessagePack::deserialize<Dfs::Reward::RequestReward>(serialized);
        if (!reward_request_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for coin reward", type);
            break;
        }
        const auto &reward_request = reward_request_result.value();
        switch (status) {
        case MessageStatus::NoStatus:
            break;
        case MessageStatus::Request: {
            node->dataMiningManager()->sendCoinsReward(reward_request);
            break;
        }
        case MessageStatus::Response: {
            switch (reward_request.TypeFunctioningObj) {
            case Dfs::Reward::TypeFunctioning::Test: {
                eLog("[TEST] You could receive {}", reward_request.RewardAmount);
                break;
            }
            case Dfs::Reward::Base:
                break;
            }
            break;
        }
        default:
            break;
        }
        break;
    }

    case MessageType::NewListConnections: {
        auto new_connection_result = MessagePack::deserialize<DfsP::Connection>(serialized);
        if (!new_connection_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for new connection", type);
            break;
        }
        node->connectionsManager()->addNewConnection(new_connection_result.value());
        node->connectionsManager()->addActivity(new_connection_result.value());
        break;
    }

    case MessageType::GetListConnections: {
        auto connection_result = MessagePack::deserialize<DfsP::Connection>(serialized);
        if (!connection_result.has_value()) {
            eWarning("[NetworkManager] {} deserialization failed for get connections", type);
            break;
        }
        node->connectionsManager()->addConnection(connection_result.value());
        for (const auto &active_connection : node->connectionsManager()->getActiveConnection()) {
            this->send_message(active_connection, MessageType::NewListConnections);
        }
        this->send_message("", MessageType::ProcessNewConnections);
        break;
    }

    case MessageType::ProcessNewConnections: {
        node->connectionsManager()->tryToNewConnect();
        break;
    }

    default: {
        std::string error_message =
            fmt::format("[NetworkManager/messageReceived] Not supported message type: {}", type);
        eFatal("{}", error_message.data());
        break;
    }
    }
}

void NetworkManager::removeWsConnection() {
    if (QObject::sender() == nullptr)
        return;

    auto connection = qobject_cast<SocketService *>(QObject::sender());
    auto removed    = m_connections->erase(connection);
    eLog("[WS] Removed {}", fmt::ptr(connection));
    //    m_reconnections.remove(NetworkReconnect {
    //        .ip = connection->ip(), .port = connection->port(), .protocol = Network::Protocol::WebSocket
    //        });
    connection->deleteLater();
    checkConnectionsStatus();
}

void NetworkManager::socketError(Network::SocketServiceError error, QString errorData) {
    if (QObject::sender() == nullptr) {
        return;
    }

    auto service = qobject_cast<SocketService *>(QObject::sender());
    eLog("[NetworkManager] Error socket: {} {}", error, service->identifier());

    if (error != Network::SocketServiceError::DuplicateIdentifier
        && error != Network::SocketServiceError::IncompatibleIdentifier) {
        auto m_reconnectionsToIdentifierLocked = *m_reconnectionsToIdentifier;
        for (auto it = m_reconnectionsToIdentifierLocked->begin(); it != m_reconnectionsToIdentifierLocked->end();
             ++it) {
            if (it->first.ip == service->ip() && it->first.protocol == service->protocol()) {
                auto r = it->first;
                auto i = it->second;
                QTimer::singleShot(1000, [this, r, i] {
                    this->reconnectSocket(r, i);
                });
                break;
            }
        }
    }

    if (error == Network::SocketServiceError::IncompatibleNetwork
        || error == Network::SocketServiceError::IncompatibleVersion) {
        emit connectionError(error, service->identifier(), errorData);
    }
}

void NetworkManager::localInizialization() {
    eLog("Doesn't find service. Start find local service");
    connect(&m_networkStatus, &NetworkStatus::statusChanged, [](NetworkStatus::Status status) {
        eLog("[NetworkStatus] {}", status);
    });

    local = std::make_shared<QNetworkAddressEntry>(Utils::findLocalIp(Utils::PrintDebug::Off));
    eLog("[NetworkManager] Found local IP: {}", local->ip().toString());

    if (!local) {
        eLog("[NetworkManager] Local not found");
        return;
    }

    bool sub = local->ip().isInSubnet(QHostAddress::parseSubnet("192.168.0.0/16"));
    eLog("Sub: {}", sub);

    if (!sub) {
        // startDiscovery();
        return;
    }

    upnpDis = std::make_unique<UPNPConnection>(local);
    upnpNet = std::make_unique<UPNPConnection>(local);
    // connect(upnpNet, &UPNPConnection::success, this, &NetworkManager::);
    // connect(upnpDis, &UPNPConnection::success, this, &NetworkManager::startDiscovery);
    connect(upnpNet.get(), &UPNPConnection::upnpError, [](QString msg) {
        eLog("[NetworkManager] UPnP error: {}", msg);
    });
    connect(upnpDis.get(), &UPNPConnection::upnpError, [](QString msg) {
        eLog("[NetworkManager] UPnP Discovery error: {}", msg);
    });
    // eLog("Tunnel creation started!");
    // upnpDis->makeTunnel(extPort, extPort, " UDP ", "Discovery tunnel of ExtraChain ");
    // upnpNet->makeTunnel(tcpPort, tcpPort, "TCP", "Network tunnel of ExtraChain ");
}

std::string NetworkManager::getNetworkVPNHash() noexcept {
    return m_networkHashForVPN;
}

void NetworkManager::setNetworkVPNHash() noexcept {
    boost::mt11213b                           rng(std::chrono::system_clock::now().time_since_epoch().count());
    boost::random::uniform_int_distribution<> dist(0, INT_MAX);
    std::string                               salt = Tools::typeToStdStringBytes<int>(dist(rng));

    KeyPrivate key;
    key.generate();
    m_networkHashForVPN =
        Utils::calculate_hash(ByteArray(key.public_key()).toString()
                                  + node->accountController()->mainActor()->id().to_string() + salt,
                              Utils::HashAlgorithm::Sha3_512)
            .substr(0, 64);
}

QString NetworkManager::localIp() {
    return local->ip().toString();
}

void NetworkManager::onNewWsConnection() {
    eInfo("NetworkManager::onNewWsConnection()");
    auto ws = wsServer->nextPendingConnection();
    if (ws == nullptr)
        eFatal("[WS] Error: ws == nulltpr");

    bool needToDelete = false;
    if (m_connections->size() >= Network::maxConnections) {
        if (!removeOneConnection()) {
            eLog(
                "[NetworkManager] Can't connect from WS server because the maximum number of "
                "constant connections reached!");
            needToDelete = true;
        }
    }

    auto service = new WebSocketService(ws, node, this, false, needToDelete);
    connectWsService(service);
    if (!needToDelete)
        m_reconnectionsToIdentifier->emplace(NetworkReconnect { .ip       = service->ip(),
                                                                .port     = service->port(),
                                                                .protocol = Network::Protocol::WebSocket },
                                             "");
}

bool NetworkManager::removeOneConnection() {
    auto connectionsLocked = *m_connections;
    bool isChanged         = false;
    for (auto it = connectionsLocked->begin(); it != connectionsLocked->end(); ++it) {
        if (!(*it)->isConstant()) {
            eLog("[NetworkManager] Socket with ip {} was changed to another", (*it)->ip());
            connectionsLocked->erase(it);

            NetworkReconnect tempConnection { .ip       = (*it)->ip(),
                                              .port     = (*it)->port(),
                                              .protocol = Network::Protocol::WebSocket };

            auto reconnectionsToIdentifierLocked = *m_reconnectionsToIdentifier;
            auto findRes                         = reconnectionsToIdentifierLocked->find(tempConnection);
            if (findRes != reconnectionsToIdentifierLocked->end())
                reconnectionsToIdentifierLocked->erase(tempConnection);

            isChanged = true;
            break;
        }
    }
    return isChanged;
}

CalculateTraffic *CalculateTraffic::GetInstance() {
    if (calculateTraffic_ == nullptr) {
        calculateTraffic_ = new CalculateTraffic();
    }
    return calculateTraffic_;
}

void CalculateTraffic::addBytesSent(const std::string &ip, qint64 bytes) {
    std::unique_lock<std::shared_mutex> lock(m_mutex); // Lock mutex for thread safety
    m_trafficStats[ip].bytesSent += bytes;
}

void CalculateTraffic::addBytesReceived(const std::string &ip, qint64 bytes) {
    std::unique_lock<std::shared_mutex> lock(m_mutex); // Lock mutex for thread safety
    m_trafficStats[ip].bytesReceived += bytes;
}

qint64 CalculateTraffic::totalBytesSentFromConnection(const std::string &ip) {
    std::shared_lock<std::shared_mutex> lock(m_mutex); // Lock mutex for thread safety
    auto                                it = m_trafficStats.find(ip);
    return (it != m_trafficStats.end()) ? it->second.bytesSent : 0;
}

qint64 CalculateTraffic::totalBytesReceivedFromConnection(const std::string &ip) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto                                it = m_trafficStats.find(ip);
    return (it != m_trafficStats.end()) ? it->second.bytesReceived : 0;
}

std::pair<std::uint64_t, std::uint64_t> CalculateTraffic::totalBytes() {
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

QString NetworkManager::foundCurrentIdentifier(QString ip, quint16 port) {
    QString res;
    auto    m_reconnectionsToIdentifierLocked = *m_reconnectionsToIdentifier;
    for (auto it = m_reconnectionsToIdentifierLocked->begin(); it != m_reconnectionsToIdentifierLocked->end();
         ++it) {
        if (it->first.ip == ip && it->first.port == port) {
            res = it->second;
            break;
        }
    }
    return res;
}

std::pair<QString, QString> NetworkManager::getPublicIPAndCountry() {
    try {
        QNetworkAccessManager manager;
        QNetworkRequest       request(QUrl("http://ip-api.com/json"));
        request.setTransferTimeout(5000);
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

        ip      = jsonObj.value("query").toString();
        country = jsonObj.value("country").toString();

        return { ip, country };
    } catch (const std::exception &error) {
        eCritical("Get public ip error: {}", error.what());
        return {};
    } catch (...) {
        eCritical("Get public ip error unknown");
        return {};
    }
}
