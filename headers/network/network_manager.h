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

#include "datastorage/block.h"
#include "datastorage/blockchain.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "network/network_status.h"
#include "network/packages/message_body.h"
#include "utils/exc_utils.h"

class ResolveManager;
class SocketService;
class TcpSocketService;
class WebSocketService;
class TcpServerService;
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
 * Creates Discovery, Resolver, Server and Sockets services
 */
class EXTRACHAIN_EXPORT NetworkManager : public QObject {
    Q_OBJECT

private:
    bool reservedActorListUse = false;
    bool active = false;
    BigNumber maxBlockCount; // latest known block num in the blockchain
    UPNPConnection *upnpDis;
    UPNPConnection *upnpNet;
    QMap<QByteArray, int> msgHashList = {};

    ExtraChainNode *node;
    ResolveManager *resolveManager;
    QNetworkAddressEntry *local = nullptr;
    TcpServerService *tcpServer = nullptr;
    QWebSocketServer *wsServer = nullptr;
    QList<SocketService *> m_connections;
    QSet<NetworkReconnect> m_reconnections;
    NetworkStatus m_networkStatus;

    std::map<std::string, MessageIdDataWaiting> m_messages_waiting;
    std::map<std::string, MessageIdDataReceived> m_messages_received;

public:
    NetworkManager(ExtraChainNode *node);
    ~NetworkManager();

    // protected:
    quint16 tcpPort = 2222;
    quint16 wsPort = 2233;

private:
    void connectTcpSocket(TcpSocketService *service);
    void connectWsService(WebSocketService *ws);

public:
    const QList<SocketService *> &connections() const;
    bool serverStatus(Network::Protocol protocol) const;

public slots:
    void removeConnection(const QString &identifier);

signals:
    void finished(); // ThreadPool

protected:
    /**
     * @brief Creates new tcp socket connection and adds it to connections
     * @param ip
     * @param port
     */
    void connectToTcpSocket(const QString &ip, quint16 port);
    void connectToWebSocket(const QString &ip, quint16 port);

    /**
     * @brief NetworkManager::checkMsgCount
     * @param msg
     * @return
     */
    bool checkMsgCount(const QByteArray &msg);
    void saveToCacheOld(const QByteArray &message, const unsigned int &msgType, const SocketPair &receiver,
                        Config::Net::TypeSend typeSend);
    void sendFromCacheOld();

private slots:
    void onNewWsConnection();

protected slots:
    void onNewTcpConnection(qint64 socketDescriptor);
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
    /**
     * @brief Remove connections from connection list
     */
    void removeTcpConnection();
    void removeWsConnection();
    void socketError(Network::SocketServiceError error, QString errorData);

public:
    QString localIp(); // TODO: remove
    void send(const QByteArray &message, const unsigned int &msgType,
              const SocketPair &receiver = SocketPair(),
              Config::Net::TypeSend typeSend = Config::Net::TypeSend::Default);

    virtual void sendMessageOld(const QByteArray &message, const unsigned int &msgType,
                                const SocketPair &receiver = {},
                                Config::Net::TypeSend typeSend = Config::Net::TypeSend::Default);

    void sendMessage(const std::string &serialized_message, Config::Net::TypeSend typeSend,
                     const std::string &receiver_identifier);
    void saveToCache(const std::string &serialized_message, Config::Net::TypeSend typeSend,
                     const std::string &receiver_identifier);
    void sendFromCache();
    bool isActiveConnectionExists();

    virtual void messageReceivedOld(const QByteArray &msg, const SocketPair &receiver);
    void messageReceived(const std::string &message, const std::string &receiver);

    void setResolveManager(ResolveManager *value);

    template <class T>
    std::string send_message(T data, MessageType type, MessageStatus status, std::string to_message_id = "",
                             Config::Net::TypeSend typeSend = Config::Net::TypeSend::All) {
        if (node->accountController()->getAccountCount() == 0) {
            qFatal("Can't send");
        }

        auto &mainActor = node->accountController()->mainActor();
        MessageBody<T> message = make_message(data, type, status, mainActor.id(), to_message_id);
        auto serialized = message.serialize();
        auto sign = mainActor.key().sign(serialized);

        std::string receiver_identifier;
        this->sendMessage("ExCNew" + serialized + sign, typeSend, receiver_identifier);
        // if (to_message_id.empty()) { // move to second send part
        //     this->m_waiting_messages.insert(sended_message_id, MessageIdData {});
        // }

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
