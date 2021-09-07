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

#include <algorithm>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>
#include <QWebSocketServer>
#include <QRandomGenerator>
#include <QMutex>

#include "utils/exc_utils.h"
#include "datastorage/block.h"
#include "datastorage/blockchain.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "managers/thread_pool.h"
#include "network/upnpconnection.h"
//#include "network/socket_pair.h"

class ResolveManager;
// class SocketService;
// class TcpSocketService;
// class WebSocketService;
// class TcpServerService;
#include "network/tcpsocket_service.h"
#include "network/websocket_service.h"
#include "network/tcpserver_service.h"
#include "network/packages/service/all_messages.h"

/**
 * @brief The NetworkManager class
 * Creates Discovery, Resolver, Server and Sockets services
 */
// static QMutex mutex;
class NetworkManager : public QObject
{
    Q_OBJECT

private:
    bool reservedActorListUse = false;
    bool active = false;
    BigNumber maxBlockCount; // latest known block num in the blockchain
    UPNPConnection *upnpDis;
    UPNPConnection *upnpNet;
    QMap<QByteArray, int> msgHashList = {};

#ifdef ECLIENT
    const int SIZE_OF_CONNECTIONS = 5;
#else
    const int SIZE_OF_CONNECTIONS = 100;
#endif
    ActorIndex *m_actorIndex;
    ResolveManager *resolveManager;
    QNetworkAddressEntry *local = nullptr;
    TcpServerService *tcpServer;
    QWebSocketServer *wsServer;
    QList<SocketService *> m_connections;
    QMap<QString, Network::Protocol> m_reconnections;

public:
    NetworkManager(ActorIndex *actorIndex, const QString &localIp = "");
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
    void startNetwork();
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
    void saveToCache(const QByteArray &message, const unsigned int &msgType, const SocketPair &receiver,
                     Config::Net::TypeSend typeSend);
    void sendFromCache();

private slots:
    void onNewWSConnection();

protected slots:
    void addTcpConnectionFromServer(qint64 socketDescriptor);
    virtual void checkConnectionsStatus();
    void startDiscovery();

public slots:
    void connectToNode(const QString &ip, Network::Protocol protocol);
    void process();
    void reconnection();

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

    virtual void sendMessage(const QByteArray &message, const unsigned int &msgType,
                             const SocketPair &receiver = {},
                             Config::Net::TypeSend typeSend = Config::Net::TypeSend::Default);
    virtual void messageReceived(const QByteArray &msg, const SocketPair &receiver);

    void setResolveManager(ResolveManager *value);

    ActorIndex *actorIndex() const;

signals:
    void newSocket();
    void connectionStatusChanged(bool status);
    void connectionsCountChanged(int socketsCount);
    void connectionError(Network::SocketServiceError error, QString identifier, QString erroData);

    friend class DfsNetworkManager;
};

#endif // NETWORK_MANAGER_H
