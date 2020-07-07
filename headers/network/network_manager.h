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
// FORWARD DECLARATION FOR CALLBACK INTEGRATION
#ifndef SERVER_SERVICE_DEF
#define SERVER_SERVICE_DEF
class ServerService;
#include "network/server_service.h"
#endif // SERVER_SERVICE

#ifndef SOCKET_SERVICE_DEF
#define SOCKET_SERVICE_DEF
class SocketService;
#include "network/socket_service.h"
#endif // SOCKET_SERVICE

#ifndef UPNP_CONNECTION_DEF
#define UPNP_CONNECTION_DEF
class UPNPConnection;
#include "network/upnpconnection.h"
#endif // UPNP_CONNECTION

#ifndef DISCOVERY_SERVICE_DEF
#define DISCOVERY_SERVICE_DEF
class DiscoveryService;
#include "network/discovery_service.h"
#endif
class ResolveManager;
//-------------------END-----------------------

#include "network/packages/service/connections_message.h"
#include <QMap>
#include <QNetworkInterface>
#include <QObject>
#include <QtCore/QThread>
#include <QtNetwork/QNetworkAddressEntry>
#include <algorithm>

#include "utils/utils.h"
#include "datastorage/block.h"
#include "datastorage/blockchain.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "managers/thread_pool.h"
#include "network/discovery_service.h"
//#include "network/resolver_service.h"
#include "network/server_service.h"
#include "network/socket_service.h"
#include "network/upnpconnection.h"
#include "network/socket_pair.h"

#include "network/packages/service/connections_message.h"

#include <QNetworkConfigurationManager>
#include <QRandomGenerator>
#include <QSettings>
#include <QMutex>
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
    quint16 extPort;
    BigNumber maxBlockCount; // latest known block num in the blockchain
    UPNPConnection *upnpDis;
    UPNPConnection *upnpNet;
    QList<QByteArray> tempConnections;
#ifdef ECONSOLE
    const int SIZE_OF_CONNECTIONS = 100;
#endif
#ifdef ECLIENT
    const int SIZE_OF_CONNECTIONS = 5;
#endif
protected:
    bool isDebug =
#ifdef QT_DEBUG
        true;
#else
        false;
#endif
    ActorIndex *actorIndex;
    AccountController *accounts;
    ResolveManager *resolveManager;
    QString serverIp = Network::serverIp;
    bool allowLocalServer = false;

    QNetworkAddressEntry *local = nullptr;

private:
    QMap<QByteArray, int> *requestResponseMap;

    ServerService *serverService;
    quint16 netPort;

private:
    QMap<QByteArray, int> handler = {};

public:
    NetManager(AccountController *accountList, ActorIndex *actorIndex);
    ~NetManager();

    void showMessage(const QHostAddress &from, const QString &message);

    void resolverMessage(const QHostAddress &from, const QString &message);
    QList<SocketService *> connections;

    quint16 serverPort = isDebug ? 2222 : 2222;

private:
    void connectSocket();
    void disconnectSocket(SocketService *connection);
    void removeConnectionByAddress(QByteArray address);
    SocketService getConnectionByAddress(const QByteArray address) const;

public:
    ServerService *getServerService();
    //    ResolverService *getResolverService();
    QList<SocketService *> getConnections() const;

protected:
    NetManager *getMe();
signals:
    void finished();

protected:
    void findLocal();
    /**
     * @brief startNetwork
     * @param serverPort
     * @param local
     * @param serverService
     */
    virtual void startNetwork();
    /**
     * @brief restoreConnections
     * @param socketList
     */
    void restoreConnections(const QList<SocketPair> &socketList);

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
     * @brief NetManager::checkMsgCount
     * @param msg
     * @return
     */
    bool checkMsgCount(const QByteArray &msg, QMap<QByteArray, int> &handler,
                       const QList<SocketService *> list);
    void saveToCache(const QByteArray &message, const unsigned int &msgType, const SocketPair &receiver,
                     Config::Net::TypeSend typeSend);
    void sendFromCache();
private slots:
    /**
     * @brief createNewConnectionsFromList
     * @param message
     */
    void createNewConnectionsFromList(const QByteArray &message);
protected slots:
    /**
     * @brief addConnection
     * @param socketDescriptor
     */
    virtual void addConnection(qint64 socketDescriptor);

    /**
     * @brief Creates new socket connection and adds it to connections
     * @param address
     * @param port
     */
    virtual SocketService *addConnectionFromPair(QHostAddress address, quint16 port);
    virtual void checkConnectionsStatus();
    void startDiscovery();
    // for upnpn
    void upnpErrDis(QString msg);
    void upnpErrNet(QString msg);

    // spread messages

public slots:
    // test thread
    void process();
    void logDebug();
    void reconnectUi();
    void connectToServerByIpList(QList<QByteArray> ipList);
    virtual void connectToServer(const quint16 &serverPort, QNetworkAddressEntry *local);
    /**
     * @brief checkMyIdentificator
     */
    void checkMyIdentificator();
    /**
     * @brief Broadcast message to all connected peers
     * @param msg
     */
    virtual void broadcastMsg(const QByteArray &msg);
    /**
     * @brief sendMessage
     * @param data for send
     * @param messageType type to compress
     */

    /**
     * @brief Remove connections from connection list
     */
    void removeConnection();

public:
    void send(const QByteArray &message, const unsigned int &msgType,
              const SocketPair &receiver = SocketPair(),
              Config::Net::TypeSend typeSend = Config::Net::TypeSend::Default);

    virtual void sendMessage(const QByteArray &message, const unsigned int &msgType,
                             const SocketPair &receiver = {},
                             Config::Net::TypeSend typeSend = Config::Net::TypeSend::Default);
    void distMessage(const QByteArray &data, const SocketPair &socketData);
    virtual void *MessageReceived(const QByteArray &msg, const SocketPair &receiver);

    //    void MoveToDfsN();

    void setResolveManager(ResolveManager *value);

    quint16 getServerPort() const;
    QString getServerIp() const;
    bool getAllowLocalServer() const;
    QNetworkAddressEntry *getLocal() const;
    QByteArray getSerializedConnectionList() const;
    void checkOnValidConnection(QByteArray id, QByteArray address);

    void addTempConnections(const QList<QByteArray> &value);

signals:
    //    void newDfsSocket(SocketService *socket);
    //    void MsgReceived(const QByteArray &msg, const SocketPair &receiver);
    //    void sendMsg(const QByteArray &data, const SocketPair &socketData);
    void newSocket();
    void qmlNetworkStatus(bool status);
    void qmlNetworkSockets(int socketsCount);
    void qmlServerError(bool serverError);
    void localIpFounded(QString localIp);
    void buildError();
};

#endif // NETWORK_MANAGER_H
