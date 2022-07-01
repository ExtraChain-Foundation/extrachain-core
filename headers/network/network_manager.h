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

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include <QtCore/QMutex>
#include <QtCore/QRandomGenerator>
#include <QtNetwork/QNetworkAddressEntry>
#include <QtNetwork/QNetworkInterface>
#include <QtNetwork/QNetworkProxy>
#include <QtWebSockets/QWebSocketServer>
#include <algorithm>
#include <string>
#include <string_view>

#include "datastorage/block.h"
#include "datastorage/blockchain.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "network/message_body.h"
#include "network/network_status.h"
#include "utils/dfs_utils.h"
#include "utils/exc_utils.h"

class SocketService;
class WebSocketService;
class UPNPConnection;

struct NetworkReconnect {
    QString ip;
    quint16 port;
    Network::Protocol protocol;
    // quint64 lastTry;
    auto operator==(const NetworkReconnect &reconnect) const {
        return ip == reconnect.ip && port == reconnect.port && protocol == reconnect.protocol;
    }
};

inline size_t qHash(const NetworkReconnect &reconnect) {
    return qHash(reconnect.ip) + qHash(reconnect.port) + qHash(int(reconnect.protocol));
}

struct MessageIdDataWaiting {
    std::string identifier;
    qint64 time;
    std::string cached_message;
    // msg type
};

struct MessageIdDataReceived {
    std::string identifier;
    qint64 time;
};

/**
 * @brief The NetworkManager class
 * Creates Discovery, Server and Sockets services
 */
class EXTRACHAIN_EXPORT NetworkManager : public QObject {
    Q_OBJECT

private:
    bool reservedActorListUse = false;
    bool active = false;
    UPNPConnection *upnpDis;
    UPNPConnection *upnpNet;
    QMap<QByteArray, int> msgHashList = {};

    ExtraChainNode &node;
    QNetworkAddressEntry *local = nullptr;
    QWebSocketServer *wsServer = nullptr;
    QList<SocketService *> m_connections;
    QSet<NetworkReconnect> m_reconnections;
    NetworkStatus m_networkStatus;

    std::map<std::string, std::string> m_messages;
    std::map<std::string, MessageIdDataWaiting> m_messages_waiting;
    std::map<std::string, MessageIdDataReceived> m_messages_received;

public:
    explicit NetworkManager(ExtraChainNode &node);
    ~NetworkManager();

    // protected:
    // quint16 tcpPort = 2222;
    quint16 wsPort = 2233;

private:
    void connectWsService(WebSocketService *ws);

public:
    const QList<SocketService *> &connections() const;
    bool serverStatus(Network::Protocol protocol) const;

public slots:
    void removeConnection(const QString &identifier);

signals:
    void finished(); // ThreadPool
    void addFragSignal(const DFSP::SegmentMessage &msg);
    void fetchFragments(DFS::Packets::RequestFileSegmentMessage &msg, std::string &messageId);

protected:
    void connectToWebSocket(const QString &ip, quint16 port);

    /**
     * @brief NetworkManager::checkMsgCount
     * @param msg
     * @return
     */
    bool checkMsgCount(const QByteArray &msg);

private slots:
    void onNewWsConnection();

protected slots:
    virtual void checkConnectionsStatus();
    void startDiscovery();

public slots:
    void startNetwork();
    void connectToNode(const QString &ip, Network::Protocol protocol);
    void process();
    void reconnection();
    void setupProxy(QNetworkProxy::ProxyType type, const QString &hostName, quint16 port, const QString &user,
                    const QString &password);

private slots:
    void removeWsConnection();
    void socketError(Network::SocketServiceError error, QString errorData);

public:
    QString localIp(); // TODO: remove

    void sendMessage(const std::string &serialized_message, Config::Net::TypeSend typeSend,
                     const std::string &receiver_identifier);
    void saveToCache(const std::string &serialized_message, Config::Net::TypeSend typeSend,
                     const std::string &receiver_identifier);
    void sendFromCache();
    bool isActiveConnectionExists();

    void messageReceived(const std::string &message, const std::string &identifier);

    template <class T>
    std::string send_message(T data, MessageType type, MessageStatus status = MessageStatus::NoStatus,
                             std::string to_message_id = "",
                             Config::Net::TypeSend typeSend = Config::Net::TypeSend::All) {
        if (status == MessageStatus::Response && to_message_id.empty()) {
            qFatal("[Network] Send message error: empty message id for response message");
        }
        if (status == MessageStatus::Response && typeSend == Config::Net::TypeSend::All) {
            qDebug()
                << "[Network] Send message warning: incorrect type send for response message, set to focused";
            typeSend = Config::Net::TypeSend::Focused;
        }

        if (node.accountController()->count() == 0) {
            // qFatal("Can't send");
            return "";
        }

        auto &mainActor = node.accountController()->mainActor();
        MessageBody<T> message = make_message(data, type, status, mainActor.id(), to_message_id);
        auto serialized = message.serialize();
        auto sign = mainActor.key().sign(serialized);
        std::string receiver_identifier;
        if (!to_message_id.empty()) {
            receiver_identifier = m_messages[to_message_id];
            //            if (receiver_identifier.empty())
            //                qFatal("Network send message error: receiver_identifier is empty");
            m_messages.erase(to_message_id);
        }

#ifdef QT_DEBUG
        if (Network::networkDebug) {
            msgpack::object_handle oh = msgpack::unpack(serialized.data(), serialized.size());
            msgpack::object deserialized = oh.get();
            qDebug() << fmt::format(
                            "[Network Message] Send: type {}, status {}, id {}, type send {}, body: {}",
                            message.message_type, message.status, message.message_id, typeSend,
                            (std::stringstream() << deserialized).str())
                            .c_str();
        }
#endif

        this->sendMessage(serialized + sign, typeSend, receiver_identifier);

        return message.message_id;
    }

signals:
    void newSocket();
    void connectionStatusChanged(bool status);
    void connectionsCountChanged(int socketsCount);
    void connectionError(Network::SocketServiceError error, QString identifier, QString erroData);

    friend class DfsNetworkManager;
};

#endif // NETWORK_MANAGER_H
