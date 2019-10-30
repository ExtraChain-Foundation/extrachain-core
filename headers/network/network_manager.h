#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "dfs/packages/headers/dfs_universal.h"
#include "network/packages/service/list_connections.h"
#include <QMap>
#include <QNetworkInterface>
#include <QObject>
#include <QtCore/QThread>
#include <QtNetwork/QNetworkAddressEntry>
#include <algorithm>

#include "headers/utils/utils.h"
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

#include "network/packages/service/list_connections.h"

#include <QNetworkConfigurationManager>
#include <QRandomGenerator>
#include <QSettings>
#include "network/packages/service/all_messages.h"
#include "network/packages/service/downloaddfsrequest.h"

/**
 * @brief The NetManager class
 * Creates Discovery, Resolver, Server and Sockets services
 */

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
    QString serverIp = "51.68.181.52";
    bool allowLocalServer = false;

    QNetworkAddressEntry *local = nullptr;

private:
    quint16 serverPort = isDebug ? 2221 : 2222;
    QMap<QByteArray, int> *requestResponseMap;

    ServerService *serverService;
    quint16 netPort;

private:
    QList<SocketService *> connections;
    QMap<QByteArray, int> handler = {};

public:
    NetManager(AccountController *accountList, ActorIndex *actorIndex);
    ~NetManager();

    void showMessage(const QHostAddress &from, const QString &message);

    void resolverMessage(const QHostAddress &from, const QString &message);

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
     * @brief restoreConnections
     * @param socketList
     */
    void restoreConnections(const QList<SocketPair> &socketList);

    void setupServerServiceConnections();
    void setupDiscoveryServiceConnections();
    /**
     * @brief signMessage
     * @param message
     */
    void signMessage(Messages::IMessage &message) const;
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
    bool checkMsgCount(const QByteArray &msg, QMap<QByteArray, int> handler);
private slots:
    /**
     * @brief createNewConnectionsFromList
     * @param message
     */
    void createNewConnectionsFromList(const QByteArray &message);
    /**
     * @brief Creates new socket connection and adds it to connections
     * @param address
     * @param port
     */
    SocketService *addConnectionFromPair(QHostAddress address, quint16 port);
    /**
     * @brief addConnection
     * @param socketDescriptor
     */
    void addConnection(qint64 socketDescriptor);
    void checkConnectionsStatus();
protected slots:
    void startNetwork(const quint16 &serverPort, QNetworkAddressEntry *local);
    void startDiscovery();
    // for upnpn
    void upnpErrDis(QString msg);
    void upnpErrNet(QString msg);

    // spread messages

public slots:
    // test thread
    void process();
    void logDebug();
    void connectToServer(const quint16 &serverPort, QNetworkAddressEntry *local);
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
    void sendMessage(const QByteArray &message);
    /**
     * @brief Remove connections from connection list
     */
    void removeConnection();
    void dfsToPeerTmp(const QByteArray &data, const QByteArray &msgType, const SocketPair &receiver);

    void MessageReceived(const QByteArray &msg, const SocketPair &receiver);

    void MoveToDfsN();

signals:
    void newDfsSocket(SocketService *socket);
    void MsgReceived(const QByteArray &msg, const SocketPair &receiver);
    void sendMsg(const QByteArray &data, const SocketPair &socketData);

    void qmlNetworkStatus(bool status);
    void qmlServerError(bool serverError);
};

#endif // NETWORK_MANAGER_H
