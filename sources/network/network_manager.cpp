/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

#include "datastorage/dfs/dfs_controller.h"
#include "managers/connections_manager.h"
#include "managers/data_mining_manager.h"
#include "managers/extrachain_node.h"
#include "managers/thread_pool.h"
#include "managers/tx_manager.h"
#include "network/upnpconnection.h"
#include "network/websocket_service.h"
#include "utils/bignumber_float.h"
#include <filesystem>

#include <fstream>
#include <vector>

CalculateTraffic *CalculateTraffic::calculateTraffic_ = nullptr;

const QList<SocketService *> &NetworkManager::connections() const {
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

std::map<NetworkReconnect, QString> &NetworkManager::reconnections() {
    return m_reconnectionsToIdentifier;
}
CalculateTraffic *NetworkManager::getCalculateTraffic() const {
    return calculateTraffic;
}

NetworkManager::NetworkManager(ExtraChainNode &node)
    : node(node) {
    localInizialization();
    m_reconnectTimer = new QTimer(this);
    calculateTraffic = CalculateTraffic::GetInstance();
}

void NetworkManager::process() {
    if (!node.isClientApp())
        return;
    connect(m_reconnectTimer, &QTimer::timeout, this, &NetworkManager::reconnection);
    m_reconnectTimer->start(Utils::ReconnectInterval);
}

void NetworkManager::reconnection() {
    qDebug() << "Count reconnections" << m_reconnectionsToIdentifier.size();
    for (auto it = m_reconnectionsToIdentifier.begin(); it != m_reconnectionsToIdentifier.end(); ++it)
        connectToWebSocket(it->first.ip, it->first.port);
}

void NetworkManager::reconnectSocket(const NetworkReconnect& connectInfo, QString identifier) {
    qDebug() << "Reconnect socket: " << connectInfo.ip << connectInfo.port;
    for (auto it = m_connections.begin(); it != m_connections.end(); ++it)
    {
        if ((*it)->identifier() == identifier)
        {
            emit (*it)->close();
            emit (*it)->finished();
        }
        break;
    }

    connectToWebSocket(connectInfo.ip, connectInfo.port);
}

void NetworkManager::setupProxy(QNetworkProxy::ProxyType type, const QString &hostName, quint16 port,
                                const QString &user, const QString &password) {
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
    connect(service, &WebSocketService::activated, this, [&] { node.dfs()->requestDirFileAllActors(); });
    if (!m_connections.contains(service)) {
        m_connections.append(service);
        if (node.isClientApp() && requestListNodes)
            send_message(std::string {}, MessageType::RequestListNodes, MessageStatus::Request);
    }
    connect(service, &WebSocketService::shareConnections, this, [&](const std::string& identifier, const QString ip, const quint16 port)
    {
        qInfo() << "shareConnections" << identifier << ip << port;
        bool isUpdated = false;
        for (auto it = m_reconnectionsToIdentifier.begin(); it != m_reconnectionsToIdentifier.end(); ++it)
        {
            if (it->first.ip == ip && it->first.port == port)
            {
                qInfo() << "shareConnections updated";
                isUpdated = true;
                it->second = QString::fromStdString(identifier);
            }
        }

        if (!isUpdated)
            m_reconnectionsToIdentifier.emplace(NetworkReconnect{.ip = ip, .port = port, .protocol = Network::Protocol::WebSocket}, QString::fromStdString(identifier));

        auto       mainActor = node.accountController()->mainActor();
        MessageBody message   =
            make_message("", MessageType::ShareConnections, MessageStatus::Request, mainActor->id(), "");
        auto        serialized = message.serialize();
        auto        sign       = mainActor->key().sign(serialized);
        this->sendMessage(serialized + sign, Config::Net::TypeSend::Focused, identifier);
    });
}

void NetworkManager::removeConnection(const QString &identifier) {
    if (identifier.isEmpty())
        qFatal("Try remove with empty identifier");

    for (auto connection : qAsConst(m_connections)) {
        if (connection->identifier() == identifier)
            emit connection->close();
    }
}

NetworkManager::~NetworkManager() {
    delete upnpNet;
    delete upnpDis;
    delete local;

    for (const auto &connection : qAsConst(m_connections)) {
        emit connection->close();
        emit connection->finished();
    }
    m_connections.clear();
}

void NetworkManager::checkConnectionsStatus() {
    m_reconnectTimer->stop();
    bool flag = false;
    int count = 0;
    std::for_each(m_connections.begin(), m_connections.end(), [&](SocketService *el) {
        flag = flag || el->isActive();
        if (el->isActive()) {
            count++;
        }
    });
    emit connectionStatusChanged(flag);
    emit connectionsCountChanged(count); // TODO: check prev count value

    if (flag) { // TODO: replace to networkStatusChanged slot
        sendFromCache();
    }
}

void NetworkManager::startNetwork() {
    qDebug() << "[NetworkManager] Start servers..." << (wsPort == 2222 ? "Network" : "DFS");

    if (local == nullptr) {
        qDebug() << "[NetworkManager] Can't detect local ip";
        return;
    }

    if (!Network::isStartedServer)
        return;
    wsServer = new QWebSocketServer("ExtraChain", QWebSocketServer::SslMode::NonSecureMode);

    if (wsServer->listen(QHostAddress::Any, wsPort)) {
        connect(wsServer, &QWebSocketServer::newConnection, this, &NetworkManager::onNewWsConnection);
        connect(wsServer, &QWebSocketServer::serverError, [](QWebSocketProtocol::CloseCode closeCode) {
            qDebug() << "[WS] Server error code:" << closeCode;
        });
        connect(wsServer, &QWebSocketServer::closed, [] { qDebug() << "[WS] Server: closed"; });
        connect(wsServer, &QWebSocketServer::acceptError, [](QAbstractSocket::SocketError socketError) {
            qDebug() << "[WS] Server socker error:" << socketError;
        });

        qDebug().noquote() << "[WS] Start listening" << wsServer->serverAddress().toString()
                           << wsServer->serverPort() << wsServer->serverName();
        DFS::Packets::WSConnection wsConnection { .address = local->ip().toString().toStdString(),
                                                  .port = static_cast<uint64_t>((int)wsPort) };
        m_wsConnections.push_back(wsConnection);
    } else if (!wsServer->listen(QHostAddress::Any, wsPort) /* && !node.isClientApp()*/) {
        bool listen = false;
        connectToNode(local->ip().toString(), Network::Protocol::WebSocket);

        while (!listen) {
            wsPort += 1;
            if (wsServer->listen(QHostAddress::Any, wsPort)) {
                connect(wsServer, &QWebSocketServer::newConnection, this, &NetworkManager::onNewWsConnection);

                connect(wsServer, &QWebSocketServer::serverError,
                        [](QWebSocketProtocol::CloseCode closeCode) {
                            qDebug() << "[WS] Server error code:" << closeCode;
                        });
                connect(wsServer, &QWebSocketServer::closed, [] { qDebug() << "[WS] Server: closed"; });
                connect(wsServer, &QWebSocketServer::acceptError,
                        [](QAbstractSocket::SocketError socketError) {
                            qDebug() << "[WS] Server socker error:" << socketError;
                        });

                qDebug().noquote() << "[WS] Start listening" << wsServer->serverAddress().toString()
                                   << wsServer->serverPort() << wsServer->serverName();
                listen = true;
                if (listen) {
                    DFS::Packets::WSConnection wsConnection { .address = local->ip().toString().toStdString(),
                                                              .port = static_cast<uint64_t>((int)wsPort) };
                    send_message(wsConnection, MessageType::NewNodeConnected);
                    m_wsConnections.push_back(wsConnection);

                    //            //diconnect from all servers
                    //            for(auto connection : m_connections) {
                    //                connection->disconnected();
                    //            }
                }
            }
        }
    }
}

[[maybe_unused]] void NetworkManager::startDiscovery() {
    qDebug() << "NetworkManager::startDiscovery()";
    // discoveryService = new DiscoveryService(extPort, tcpPort, local);
    // ThreadPool::addThread(discoveryService);
    // connect(discoveryService, &DiscoveryService::ClientDiscovered, this,
    // &NetworkManager::addConnectionFromPair);
}

void NetworkManager::connectToNode(const QString &ip, Network::Protocol protocol, const bool request) {
    if (m_connections.length() >= Network::maxConnections) {
        qDebug() << "[NetworkManager] Can't connect because the maximum number of connections";
        return;
    }

    if (ip.isEmpty())
        return;

    const quint16 port = (protocol == Network::Protocol::WebSocket ? wsPort : 0);
    qDebug().noquote().nospace() << QString("[NetworkManager] Connect to %1. protocol: %2. port: %3")
                                        .arg(ip)
                                        .arg((int)protocol)
                                        .arg(port);
    //    m_reconnections.insert(NetworkReconnect { .ip = ip, .port = port, .protocol = protocol });

    using Network::Protocol;
    switch (protocol) {
    case Protocol::Udp:
        break;
    case Protocol::WebSocket:
        connectToWebSocket(ip.simplified(), port, request);
        break;
    case Protocol::Undefined:
        qFatal("Undefined connectToNode");
    }
}

void NetworkManager::connectToWebSocket(const QString &ip, quint16 port, bool requestListNodes) {
    auto service = new WebSocketService(nullptr, node);
    service->open(ip, port);
    connectWsService(service, requestListNodes);
    m_reconnectionsToIdentifier.emplace(NetworkReconnect{ .ip = ip, .port = port, .protocol = Network::Protocol::WebSocket }, "");

}

void NetworkManager::sendMessage(const std::string &serialized_message, Config::Net::TypeSend type_send,
                                 const std::string &receiver_identifier) {
    if (!isActiveConnectionExists()) {
        qDebug() << "[NetworkManager] Save message to cache";
        saveToCache(serialized_message, type_send, receiver_identifier);
        return;
    }

    static auto isSendCheck = [](const Config::Net::TypeSend &type_send,
                                 const std::string &receiver_identifier,
                                 const std::string &socket_identifier) {
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

    for (const auto &service : std::as_const(m_connections)) {
        if (service->isActive()
            && isSendCheck(type_send, receiver_identifier, service->identifier().toStdString())) {
            calculateTraffic->addBytesSent(service->ip().toStdString(), serialized_message.size());
            service->sendMessage(QByteArray::fromStdString(serialized_message));
        }
    }
}

void NetworkManager::saveToCache(const std::string &serialized_message, Config::Net::TypeSend typeSend,
                                 const std::string &receiver_identifier) {
    std::ofstream file;
    file.open(NetworkCacheFile, std::ios_base::out | std::ios_base::app | std::ios_base::binary);
    if (!file.is_open()) {
        qFatal("[NetworkManager/saveToCache] Error open cache file");
    }
    auto size = std::filesystem::file_size(NetworkCacheFile);
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
        std::string package = Utils::bytesEncodeStdString(serializedMessage);
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
        std::string serializedMessage = Serialization::serialize(
            std::vector<std::string> { serialized_message, typeSendToString(typeSend), receiver_identifier });
        list.push_back(serializedMessage);
        file.close();
        file.open(NetworkCacheFile, std::ofstream::out | std::ofstream::trunc);
        file << Serialization::serialize(list);
        file.close();
    }
}

void NetworkManager::sendFromCache() {
    qDebug() << "[NetworkManager] Load from cache";

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
        const std::vector<std::string> deserializedList =
            Serialization::deserialize(allPackages[numberPackage]);
        if (deserializedList.size() < 3) {
            qWarning("Size deserialized data in not correct");
            continue;
        }
        const std::string deserialized_message = deserializedList[0];
        const Config::Net::TypeSend typeSend = typeSendFromString(deserializedList[1]);
        const std::string receiver_identifier = deserializedList[2];
        sendMessage(deserialized_message, typeSend, receiver_identifier);
    }
}

bool NetworkManager::isActiveConnectionExists() {
    if (this->m_connections.isEmpty())
        return false;

    for (const auto &el : qAsConst(this->m_connections)) {
        if (el->isActive())
            return true;
    }

    return false;
}

bool NetworkManager::checkMsgCount(const std::string &msg) {
    bool flag_result = true;
    bool value = 0;
    std::string hashMsg = Utils::calcHash(msg);
    QMap<std::string, int>::iterator it = msgHashList.find(hashMsg);

    if (it == msgHashList.end())
        msgHashList.insert(hashMsg, value);
    else {
        if (msgHashList.find(hashMsg).value() == m_connections.length() - 1) {
            msgHashList.remove(hashMsg);
            flag_result = false;
        } else {
            msgHashList.find(hashMsg).value()++;
            flag_result = true;
        }
    }

    return flag_result;
}

void NetworkManager::messageReceived(
    const std::string &message,
    const std::string &ip,
    const std::string &identifier) {
    if (!checkMsgCount(message)) {
        qDebug()
            << "[Network Manager] checkMsgCount have returned false: such message has been already added";
        return;
    }

    m_messages[identifier] = message;

    std::string_view msg = std::string_view(message).substr(0, message.size() - 64);
    std::string_view sign = std::string_view(message).substr(message.size() - 64, 64);

    MessageBody mb = MessagePack::deserialize<MessageBody>(msg);
    MessageType type = mb.message_type;
    MessageStatus status = mb.status;
    std::string serialized = mb.data;
    std::string messId = mb.message_id;
    std::string messageId(messId.begin(), messId.end());

    if (status == MessageStatus::Request) {
        m_messages[messageId] = identifier;
    }

#ifdef QT_DEBUG
    if (Network::networkDebug) {
        msgpack::object_handle oh = msgpack::unpack(serialized.data(), serialized.size());
        msgpack::object deserialized = oh.get();
        qDebug() << fmt::format("[Network Message] Received: type {}, status {}, id {}, body: {}", type,
                                status, messId, (std::stringstream() << deserialized).str())
                        .c_str();
    }
#endif

    calculateTraffic->addBytesReceived(ip, message.size());

    // try {
    switch (type) {
    case MessageType::ShareConnections:{
        if (status == MessageStatus::Request)
        {
            qInfo() << "Achieved ShareConnections(Request)" << messageId;
            std::vector<std::string> ips;
            for (const auto& item : m_connections)
            {
                if (identifier != item->identifier().toStdString())
                {
                    qDebug() << item->ip().toStdString();
                    ips.emplace_back(item->ip().toStdString());
                }
            }

            if (!ips.empty())
            {
                node.network()->send_message(MessagePack::serializeContainer(ips), MessageType::ShareConnections,
                                             MessageStatus::Response, messageId, Config::Net::TypeSend::Focused);
            }
        }
        else if (status == MessageStatus::Response)
        {
            qInfo() << "Achieved ShareConnections(Response)" << messageId;
            auto ipsInput = MessagePack::deserialize<std::vector<std::string>>(serialized);
            auto ips = MessagePack::deserializeContainer<std::string>(ipsInput);
            for (const auto& item : ips)
            {
                bool canConnect = true;
                for (const auto& connItem : m_connections)
                {
                    if (item == connItem->ip().toStdString())
                    {
                        canConnect = false;
                        break;
                    }
                }

                if (canConnect)
                    connectToNode(QString::fromStdString(item), Network::Protocol::WebSocket);
            }
        }
        break;
    }
    case MessageType::ResponseDfsSize: {
        const auto msgStruct = MessagePack::deserialize<DFSP::ResponseDfsSize>(serialized);
        if (Utils::globalVariableOfDfsSize < msgStruct.Size) {
            Utils::globalVariableOfDfsSize = msgStruct.Size;
        }
        break;
    }
    case MessageType::RequestDfsSize: {
        const auto msgStruct = MessagePack::deserialize<DFSP::RequestDfsSize>(serialized);
        node.dfs()->sendSizeReponseMsg(msgStruct, messageId);
        break;
    }
    case MessageType::ResponseBlockCount: {
        const auto msgStruct = MessagePack::deserialize<DFSP::ResponseBlockCount>(serialized);
        BigNumber count = msgStruct.blockCount;
        emit messageCountReceived(count);
        break;
    }
    case MessageType::RequestBlockCount: {
        const auto msgStruct = MessagePack::deserialize<DFSP::RequestBlockCount>(serialized);
        BigNumber dfsCount = node.blockchain()->getBlockCount();
        node.dfs()->sendCountReponseMsg(msgStruct, messageId, dfsCount);
        break;
    }
    case MessageType::NewActor: {
        auto actor = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
        node.actorIndex()->handleNewActor(actor);
        break;
    }
    case MessageType::Actor: {
        // actor get, test use ActorId
        if (status == MessageStatus::Request) {
            auto actorId = MessagePack::deserialize<ActorId>(serialized);
            node.actorIndex()->handleGetActor(actorId, messageId);
        } else if (status == MessageStatus::Response) {
            auto actor = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
            node.actorIndex()->handleNewActor(actor);
        }
        break;
    }
    case MessageType::ActorAll: {
        if (status == MessageStatus::Request) {
            auto ignoredActorId = MessagePack::deserialize<ActorId>(serialized);
            node.actorIndex()->handleGetAllActor(ignoredActorId, messageId);
        } else if (status == MessageStatus::Response) {
            auto actors = MessagePack::deserialize<std::vector<std::string>>(serialized);
            node.actorIndex()->handleNewAllActors(actors);
        }
        break;
    }
    case MessageType::ActorCount:
        break;

    case MessageType::DfsDirData: {
        if (status == MessageStatus::Request) {
            auto actorId = MessagePack::deserialize<ActorId>(serialized); // TODO: add last modified
            node.dfs()->sendDirData(actorId, 0, messageId);
        } else if (status == MessageStatus::Response) {
            auto [actorId, dirRows] =
                MessagePack::deserialize<std::pair<ActorId, std::vector<DFSP::DirRow>>>(serialized);
            node.dfs()->addDirData(actorId, dirRows);
        }
        break;
    }
    case MessageType::DfsLastModified: {
        auto msg = MessagePack::deserialize<uint64_t>(serialized);
        node.dfs()->sendSync(msg, messageId);
        break;
    }
    case MessageType::DfsAddFile: {
        auto msg = MessagePack::deserialize<DFSP::AddFileMessage>(serialized);
        node.dfs()->addFile(msg, true);
        break;
    }
    case MessageType::DfsRequestFile: {
        auto [actorId, fileName] = MessagePack::deserialize<std::pair<ActorId, std::string>>(serialized);
        node.dfs()->sendFile(actorId.toStdString(), fileName, messageId);
        break;
    }
    case MessageType::DfsRequestFileSegment: {
        auto msg = MessagePack::deserialize<DFSP::RequestFileSegmentMessage>(serialized);
        emit fetchFragment(msg, messageId);
        break;
    }
    case MessageType::DfsAddSegment: {
        auto msg = MessagePack::deserialize<DFSP::SegmentMessage>(serialized);
        emit addFragSignal(msg);
        break;
    }
    case MessageType::DfsEditSegment: {
        auto msg = MessagePack::deserialize<DFSP::SegmentMessage>(serialized);
        node.dfs()->insertFragment(msg);
        break;
    }
    case MessageType::DfsDeleteSegment: {
        auto msg = MessagePack::deserialize<DFSP::DeleteSegmentMessage>(serialized);
        node.dfs()->deleteFragment(msg);
        break;
    }
    case MessageType::DfsRemoveFile: {
        auto msg = MessagePack::deserialize<DFSP::RemoveFileMessage>(serialized);
        node.dfs()->removeFile(msg);
        break;
    }
    case MessageType::DfsSendingFileDone: { // TODO
        auto [actorId, fileHash] = MessagePack::deserialize<std::pair<ActorId, std::string>>(serialized);
        qDebug() << "[Dfs] File done:" << actorId << fileHash.c_str();
        break;
    }

    case MessageType::DfsVerifyList: {
        switch (status) {
        case MessageStatus::NoStatus:
            break;
        case MessageStatus::Request: {
            std::vector<std::string> listMessage =
                MessagePack::deserialize<std::vector<std::string>>(serialized);
            std::vector<DFSP::VerifyFileMessage> listVerifiedMessage =
                MessagePack::deserializeContainer<DFSP::VerifyFileMessage>(listMessage);
            node.dfs()->verifyFiles(listVerifiedMessage, messageId);
            break;
        }
        case MessageStatus::Response: {
            std::vector<std::string> listMessage =
                MessagePack::deserialize<std::vector<std::string>>(serialized);
            std::vector<DFSP::VerifyFileMessage> listVerifiedMessage =
                MessagePack::deserializeContainer<DFSP::VerifyFileMessage>(listMessage);
            float percentVerified = node.dfs()->percentVerified(listVerifiedMessage);
            break;
        }
        }
        break;
    }

    case MessageType::DfsState: {
        switch (status) {
        case MessageStatus::Request: {
            Transaction reward = node.dataMiningManager()->makeRewardTx(mb);
            this->send_message(reward, MessageType::BlockchainTransaction);
            break;
        }
        case MessageStatus::NoStatus:
        case MessageStatus::Response:
            break;
        }

        break;
    }

    case MessageType::BlockchainGenesisBlock: {
        qDebug() << "BlockchainGenesisBlock";
        // TODO: why temp std::string?
        std::string stddata = MessagePack::deserialize<std::string>(serialized);
        GenesisBlock genesisBlock(stddata);
        if (!genesisBlock.isEmpty()) {
            node.blockchain()->addGenBlockToBlockchain(genesisBlock);
        } else {
            qDebug() << "false genesis block";
        }
        break;
    }

    case MessageType::BlockchainNewBlock: {
        qDebug() << "BlockchainNewBlock";
        std::string stddata = MessagePack::deserialize<std::string>(serialized);
        Block block(stddata);
        if (!block.isEmpty()) {
            auto blockVariant = BlockVariant(block);
            node.blockchain()->addBlockToBlockchain(blockVariant);
        }
        break;
    }

    case MessageType::BlockchainTransaction: {
        qDebug() << "BlockchainTransaction";
        Transaction transaction = MessagePack::deserialize<Transaction>(serialized);
        if (!(transaction.getData().empty()) && (transaction.getTypeTx() != TypeTx::RewardTransaction)) {
            TransactionData transactionData =
                MessagePack::deserialize<TransactionData>(transaction.getData());
            qDebug() << "run code from " << transactionData.path.c_str()
                     << "with hash: " << transactionData.hash.c_str();
        }

        // TODO deep analisys
        auto &transactionList = node.txManager()->getReceivedTxListByReference();
        auto found = std::find(transactionList.begin(), transactionList.end(), transaction);
        if (found != transactionList.end()) {
            transactionList.erase(found);
        }
        node.txManager()->addTransaction(transaction);
        break;
    }

    case MessageType::BlockchainCopyScript: {
        auto msg = MessagePack::deserialize<DFSP::RequestFileSegmentMessage>(serialized);
        std::string fromPath = DFS::Basic::fsActrRoot + "/" + msg.Actor + "/" + msg.FileName;
        std::string toPath = Scripts::folder + "/" + msg.FileName;
        std::filesystem::copy_file(fromPath, toPath);
        break;
    }

    case MessageType::BlockchainRequestBlock: {
        qDebug() << "BlockchainRequestBlock";
        std::pair<BlockType, BigNumber> requestData =
            MessagePack::deserialize<std::pair<BlockType, BigNumber>>(serialized);
        if (requestData.first == BlockType::Data)
            node.blockchain()->sendBlockByNumber(requestData.second);
        else if (requestData.first == BlockType::Genesis)
            node.blockchain()->sendLastGenesisBlock();

        break;
    }

    case MessageType::FragmentDataInfo: {
        auto msg = MessagePack::deserialize<DFSF::FragmentsInfo>(serialized);
        msg.print();
        break;
    }

    case MessageType::FragmentsDataListInfo: {
        auto fragmentsInfoList = MessagePack::deserialize<std::vector<DFSF::FragmentsInfo>>(serialized);
        qDebug() << "Recieved fragment data info from list";
        for (const auto &msg : fragmentsInfoList) {
            msg.print();
        }
        break;
    }

    case MessageType::BlockchainCoinReward: {
        auto requestReward = MessagePack::deserialize<DFSR::RequestReward>(serialized);
        switch (status) {
        case MessageStatus::NoStatus:
            break;
        case MessageStatus::Request: {
            node.blockchain()->sendCoinsReward(requestReward);
            break;
        }
        case MessageStatus::Response: {
            switch (requestReward.TypeFunctioningObj) {
            case DFSR::TypeFunctioning::Test: {
                qDebug() << "[TEST] You could receive" << requestReward.RewardAmount;
                break;
            }
            case DFS::Reward::Base:
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
        auto connection = MessagePack::deserialize<DFSP::Connection>(serialized);
        node.connectionsManager()->addNewConnection(connection);
        node.connectionsManager()->addActivity(connection);
        break;
    }

    case MessageType::GetListConnections: {
        DFSP::Connection connection = MessagePack::deserialize<DFSP::Connection>(serialized);
        node.connectionsManager()->addConnection(connection);
        for (const auto &connection : node.connectionsManager()->getActiveConnection()) {
            this->send_message(connection, MessageType::NewListConnections);
        }
        this->send_message("", MessageType::ProcessNewConnections);

        break;
    }
    case MessageType::ProcessNewConnections: {
        node.connectionsManager()->tryToNewConnect();
        break;
    }

    case MessageType::NewNodeConnected: {
        qDebug() << "Get new node";
        DFSP::WSConnection wsConnection = MessagePack::deserialize<DFSP::WSConnection>(serialized);
        m_reconnectionsToIdentifier.emplace(NetworkReconnect::fromWsConnection(wsConnection), "");
        m_wsConnections.push_back(wsConnection);
        if (!node.isClientApp()) {
            send_message(wsConnection, MessageType::SpreadNodeConnection);
        }
        break;
    }

    case MessageType::SpreadNodeConnection: {
        qDebug() << "received new node connection";
        DFSP::WSConnection wsConnection = MessagePack::deserialize<DFSP::WSConnection>(serialized);
        if (wsConnection.address == local->ip().toString().toStdString() && wsConnection.port == wsPort) {
            qDebug() << "[WS] connection port" << wsPort << "address:" << local->ip().toString();
        } else {
            connectToWebSocket(QString::fromStdString(wsConnection.address), wsConnection.port, true);
        }
        break;
    }

    case MessageType::RequestListNodes: {
        if (status == MessageStatus::Response && node.isClientApp()) {
            std::vector<std::string> newWsConnectionsList =
                MessagePack::deserialize<std::vector<std::string>>(serialized);
            std::vector<DFSP::WSConnection> newWSConnections =
                MessagePack::deserializeContainer<DFSP::WSConnection>(newWsConnectionsList);

            for (const auto &c : newWSConnections) {
                m_reconnectionsToIdentifier.emplace(NetworkReconnect { .ip = QString::fromStdString(c.address),
                                                                      .port = static_cast<quint16>(c.port),
                                                                      .protocol = Network::Protocol::WebSocket }, "");
                wsPort = c.port;
                connectToWebSocket(QString::fromStdString(c.address), wsPort, false);
            }

            qDebug() << "count reconnect urls:" << m_reconnectionsToIdentifier.size();
            for (auto it = m_reconnectionsToIdentifier.begin(); it != m_reconnectionsToIdentifier.end(); ++it)
                it->first.print();

        } else if (status == MessageStatus::Request) {
            requestWSNodeList(messageId);
        }

        break;
    }

    case MessageType::Accrual: {
        auto actor = MessagePack::deserialize<Actor<KeyPublic>>(serialized);
        qDebug() << "Begin accrual for actor " << actor.id().toString();
        Transaction tx(ActorId(), actor.id(), BigNumberFloat("1000", NumSystem::DEC), ActorId(Token::ROCC_TOKEN));
        tx.setDate(QDateTime::currentMSecsSinceEpoch());
        tx.setData(fmt::format("accrual:{}", actor.id().toStdString()));
        node.txManager()->addTransaction(tx);
        node.network()->send_message(tx, MessageType::BlockchainTransaction);
        break;
    }

    default:
        qFatal("[NetworkManager/messageReceived] Not supported message type: %d", int(type));
        break;
    }
    // } catch (std::exception e) { qFatal("[NetworkManager/messageReceived] Error deserialize"); }
}

void NetworkManager::requestWSNodeList(std::string message_id) {
    qDebug() << "requestWSNodeList" << m_wsConnections.size();
    if (m_wsConnections.empty() || node.isClientApp())
        return;

    std::vector<std::string> serializedData = MessagePack::serializeContainer(m_wsConnections);
    send_message(serializedData, MessageType::RequestListNodes, MessageStatus::Response, message_id);
}

void NetworkManager::removeWsConnection() {
    if (QObject::sender() == nullptr)
        return;

    auto connection = qobject_cast<SocketService *>(QObject::sender());
    auto removed = m_connections.removeAll(connection);
    qDebug() << "[WS] Removed" << connection;
    //    m_reconnections.remove(NetworkReconnect {
    //        .ip = connection->ip(), .port = connection->port(), .protocol = Network::Protocol::WebSocket });
    connection->deleteLater();
    checkConnectionsStatus();
}

void NetworkManager::socketError(Network::SocketServiceError error, QString errorData) {
    if (QObject::sender() == nullptr) {
        return;
    }

    auto service = qobject_cast<SocketService *>(QObject::sender());
    qDebug() << "[NetworkManager] Error socket:" << error << service->identifier();

    if (error != Network::SocketServiceError::DuplicateIdentifier) {
        for (auto it = m_reconnectionsToIdentifier.begin(); it != m_reconnectionsToIdentifier.end(); ++it)
        {
            if (it->first.ip == service->ip() && it->first.protocol == service->protocol())
            {
                reconnectSocket(it->first, it->second);
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
    qDebug() << "Doesn't find service. Start find local service.";
    connect(&m_networkStatus, &NetworkStatus::statusChanged,
            [](NetworkStatus::Status status) { qDebug() << "[NetworkStatus]" << status; });

    local = new QNetworkAddressEntry(Utils::findLocalIp(Utils::PrintDebug::Off));
    qDebug().noquote() << "[NetworkManager] Found local IP:" << local->ip().toString();

    if (local == nullptr) {
        qDebug() << "[NetworkManager] Local not found";
        return;
    }

    bool sub = local->ip().isInSubnet(QHostAddress::parseSubnet("192.168.0.0/16"));
    qDebug() << "Sub:" << sub;

    if (!sub) {
        // startDiscovery();
        return;
    }

    upnpDis = new UPNPConnection(*local);
    upnpNet = new UPNPConnection(*local);
    // connect(upnpNet, &UPNPConnection::success, this, &NetworkManager::);
    // connect(upnpDis, &UPNPConnection::success, this, &NetworkManager::startDiscovery);
    connect(upnpNet, &UPNPConnection::upnpError,
            [](QString msg) { qDebug() << "[NetworkManager] UPnP error:" << msg; });
    connect(upnpDis, &UPNPConnection::upnpError,
            [](QString msg) { qDebug() << "[NetworkManager] UPnP Discovery error:" << msg; });
    // qDebug() << "Tunnel creation started!";
    // upnpDis->makeTunnel(extPort, extPort, " UDP ", "Discovery tunnel of ExtraChain ");
    // upnpNet->makeTunnel(tcpPort, tcpPort, "TCP", "Network tunnel of ExtraChain ");
}

std::string NetworkManager::getNetworkVPNHash() noexcept {
    return m_networkHashForVPN;
}

void NetworkManager::setNetworkVPNHash() noexcept {
    boost::mt11213b rng(std::chrono::system_clock::now().time_since_epoch().count());
    boost::random::uniform_int_distribution<> dist(0, INT_MAX);
    std::string salt = Tools::typeToStdStringBytes<int>(dist(rng));

    KeyPrivate key;
    key.generate();
    m_networkHashForVPN =
        Utils::calcHash(key.publicKey() + node.accountController()->mainActor()->id().toString().toStdString()
                            + salt,
                        Utils::HashEncode::Sha3_512).substr(0, 64);
}

QString NetworkManager::localIp() {
    return local->ip().toString();
}

void NetworkManager::onNewWsConnection() {
    auto ws = wsServer->nextPendingConnection();
    if (ws == nullptr)
        qFatal("[WS] Error: ws == nulltpr");

    if (m_connections.length() >= Network::maxConnections) {
        qDebug() << "[NetworkManager] Can't connect from WS server because the maximum number of connections";
        return;
    }

    auto service = new WebSocketService(ws, node);
    connectWsService(service);
    emit newSocket();
    m_reconnectionsToIdentifier.emplace(NetworkReconnect {
                                    .ip = service->ip(), .port = service->port(), .protocol = Network::Protocol::WebSocket }, "");
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
    auto it = m_trafficStats.find(ip);
    return (it != m_trafficStats.end()) ? it->second.bytesSent : 0;
}

qint64 CalculateTraffic::totalBytesReceivedFromConnection(const std::string &ip) {
    std::shared_lock<std::shared_mutex> lock(m_mutex);
    auto it = m_trafficStats.find(ip);
    return (it!=m_trafficStats.end()) ? it->second.bytesReceived : 0;}

std::pair<uint64_t, uint64_t> CalculateTraffic::totalBytes() {
    std::shared_lock<std::shared_mutex> lock(m_mutex); // Lock mutex for thread safety
    return std::accumulate(m_trafficStats.begin(), m_trafficStats.end(), std::make_pair(uint64_t{0}, uint64_t{0}),
    [](std::pair<uint64_t, uint64_t> acc, const auto& connection) {
        acc.first += connection.second.bytesSent;
        acc.second += connection.second.bytesReceived;
        return acc;
    });
}
