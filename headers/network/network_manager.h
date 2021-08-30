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

class ResolveManager;

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
#include "network/socket_pair.h"
#include "network/tcpsocket_service.h"
#include "network/websocket_service.h"
#include "network/tcpserver_service.h"
#include "network/packages/service/all_messages.h"

/**
 * @brief The NetManager class
 * Creates Discovery, Resolver, Server and Sockets services
 */
// static QMutex mutex;
class NetManager : public QObject
{
    Q_OBJECT
    const int maxValueTryConnections = 3;

private:
    bool reservedActorListUse = false;
    bool active = false;
    BigNumber maxBlockCount; // latest known block num in the blockchain
    UPNPConnection *upnpDis;
    UPNPConnection *upnpNet;
    QList<QByteArray> tempConnections;

#ifdef ECLIENT
    const int SIZE_OF_CONNECTIONS = 5;
#else
    const int SIZE_OF_CONNECTIONS = 100;
#endif
protected:
    ActorIndex *actorIndex;
    AccountController *accounts;
    ResolveManager *resolveManager;
    QNetworkAddressEntry *local = nullptr;

private:
    QMap<QByteArray, int> *requestResponseMap;

    TcpServerService *serverService;
    QWebSocketServer *wsServer;
    QList<WebSocketService *> wsConnections;

private:
    QMap<QByteArray, int> handler = {};

public:
    NetManager(AccountController *accountList, ActorIndex *actorIndex, const QString &localIp = "");
    ~NetManager();

    void showMessage(const QHostAddress &from, const QString &message);

    void resolverMessage(const QHostAddress &from, const QString &message);
    QList<TcpSocketService *> tcpConnections;

    quint16 tcpPort = 2222;
    quint16 wsPort = 2233;

private:
    void connectTcpSocket(TcpSocketService *service);
    void disconnectTcpSocket(TcpSocketService *connection);
    void connectWsService(WebSocketService *ws);
    TcpSocketService getConnectionByAddress(const QByteArray address) const;
    inline int connectionsCount() const;

public:
    TcpServerService *getServerService();
    // ResolverService *getResolverService();
    const QList<TcpSocketService *> &getTcpConnections() const;
    const QList<WebSocketService *> &getWsConnections() const;
    void removeConnection(const QString &ip, quint16 port, Network::Protocol protocol);
    // TODO: removeConnection by id

protected:
    NetManager *getMe();

signals:
    void finished();

protected:
    /**
     * @brief startNetwork
     * @param serverPort
     * @param local
     * @param serverService
     */
    virtual void startNetwork();

    virtual void setupServerServiceConnections();
    void setupDiscoveryServiceConnections();
    /**
     * @brief signMessage
     * @param message
     */
    void signMessage(Messages::BaseMessage &message) const;
    /**
     * @brief calcHash
     * @param message
     * @return
     */
    QByteArray calcHash(const Messages::IMessage &message) const;

protected:
    /**
     * @brief Creates new socket connection and adds it to connections
     * @param address
     * @param port
     */
    virtual TcpSocketService *connectToTcpSocket(const QString &ip, quint16 port);
    void connectToWebSocket(const QString &ip, quint16 port);

    /**
     * @brief NetManager::checkMsgCount
     * @param msg
     * @return
     */
    bool checkMsgCount(const QByteArray &msg, QMap<QByteArray, int> &handler,
                       const QList<TcpSocketService *> list);
    void saveToCache(const QByteArray &message, const unsigned int &msgType, const SocketPair &receiver,
                     Config::Net::TypeSend typeSend);
    void sendFromCache();

private slots:
    /**
     * @brief createNewConnectionsFromList
     * @param message
     */
    void createNewConnectionsFromList(const QByteArray &message);
    void onNewWSConnection();

protected slots:
    virtual void addTcpConnectionFromServer(qint64 socketDescriptor);
    virtual void checkConnectionsStatus();
    void startDiscovery();
    // for upnpn
    void upnpErrDis(QString msg);
    void upnpErrNet(QString msg);

    // spread messages

public slots:
    /**
     * @brief addConnection
     * @param socketDescriptor
     */
    void connectToNode(const QString &ip, Network::Protocol protocol);

    void process();
    void connectToServerByIpList(QList<QByteArray> ipList);

    /**
     * @brief checkMyIdentifier
     */
    void checkMyIdentifier();
    /**
     * @brief sendMessage
     * @param data for send
     * @param messageType type to compress
     */

private slots:
    /**
     * @brief Remove connections from connection list
     */
    void removeTcpConnection();
    void removeWsConnection();

public:
    QString localIp();
    void send(const QByteArray &message, const unsigned int &msgType,
              const SocketPair &receiver = SocketPair(),
              Config::Net::TypeSend typeSend = Config::Net::TypeSend::Default);

    virtual void sendMessage(const QByteArray &message, const unsigned int &msgType,
                             const SocketPair &receiver = {},
                             Config::Net::TypeSend typeSend = Config::Net::TypeSend::Default);
    virtual void *MessageReceived(const QByteArray &msg, const SocketPair &receiver);

    // void MoveToDfsN();

    void setResolveManager(ResolveManager *value);

    quint16 getServerPort() const;
    QNetworkAddressEntry *getLocal() const;
    QByteArray getSerializedConnectionList() const;
    void checkOnValidConnection(QByteArray id, QByteArray address);
    void addTempConnections(const QList<QByteArray> &value);

signals:
    // void newDfsSocket(SocketService *socket);
    // void MsgReceived(const QByteArray &msg, const SocketPair &receiver);
    // void sendMsg(const QByteArray &data, const SocketPair &socketData);
    void newSocket();
    void networkStatusChanged(bool status);
    void networkSocketsCountChanged(int socketsCount);
    void networkErrorChanged(bool serverError);
    void webSocketsCountChanged(int count);
};

#endif // NETWORK_MANAGER_H
