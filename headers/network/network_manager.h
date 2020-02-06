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
    quint16 serverPort = isDebug ? 2221 : 2222;
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

private:
    void connectSocket();
    void disconnectSocket(SocketService *connection);

public:
    ServerService *getServerService();
    //    ResolverService *getResolverService();
    QList<SocketService *> getConnections() const;

protected:
    NetManager *getMe();
signals:
    void finished();

protected:
    /**
     * @brief Send message directly to the selected peer
     * @param msg
     * @param peerAddress
     */
    void sendMsgToPeer(Messages::IMessage &msg, QHostAddress peerAddress);
    /**
     * @brief sendMsgToPeerPort
     * @param msg
     * @param peerAddress
     * @param port
     */
    void sendMsgToPeerPort(Messages::IMessage &msg, QHostAddress peerAddress, int port);
    /**
     * @brief findLocal
     */
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
    void saveToCache(const QByteArray &message, const unsigned int &msgType, const SocketPair &receiver);
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
    virtual void sendMessage(const QByteArray &message, const unsigned int &msgType,
                             const SocketPair &receiver);
    void distMessage(const QByteArray &data, const SocketPair &socketData);
    virtual void *MessageReceived(const QByteArray &msg, const SocketPair &receiver);

    //    void MoveToDfsN();

    void setResolveManager(ResolveManager *value);

    quint16 getServerPort() const;
    QString getServerIp() const;
    bool getAllowLocalServer() const;
    QNetworkAddressEntry *getLocal() const;

signals:
    //    void newDfsSocket(SocketService *socket);
    //    void MsgReceived(const QByteArray &msg, const SocketPair &receiver);
    //    void sendMsg(const QByteArray &data, const SocketPair &socketData);
    void newSocket();
    void qmlNetworkStatus(bool status);
    void qmlNetworkSockets(int socketsCount);
    void qmlServerError(bool serverError);
    void buildError();
};

#endif // NETWORK_MANAGER_H
