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
    quint16 netPort;
    BigNumber maxBlockCount; // latest known block num in the blockchain
    QNetworkAddressEntry *local = nullptr;
    UPNPConnection *upnpDis;
    UPNPConnection *upnpNet;

    bool isDebug =
#ifdef QT_DEBUG
        true;
#else
        false;
#endif
    QString serverIp = "51.68.181.52;51.68.181.53";
    quint16 serverPort = isDebug ? 2221 : 2222;
    bool allowLocalServer = false;
    QMap<QByteArray, int> *requestResponseMap;

private:
    ActorIndex *actorIndex;
    AccountController *accounts;

    //    ResolverService *resolverService;
    //    DiscoveryService *discoveryService;
    ServerService *serverService;
    QList<SocketService *> connections;

#ifdef ETALONIUM_CLIENT
    QTcpSocket *socket_wer;
#endif

public:
    NetManager(AccountController *accountList, ActorIndex *actorIndex);
    ~NetManager();

    void showMessage(const QHostAddress &from, const QString &message);
    void sendMessageTest();

    void resolverMessage(const QHostAddress &from, const QString &message);

public:
    ServerService *getServerService();
    //    ResolverService *getResolverService();
    QList<SocketService *> getConnections() const;

signals:
    void finished();

private:
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
    // create connect from signal to slot
    //    void setupActorIndexConnections();
    void setupServerServiceConnections();
    void setupDiscoveryServiceConnections();
    //    void setupResolverServiceConnections();
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
    QByteArray calcHash(Messages::IMessage &message) const;
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
    /**
     * @brief Remove connections from connection list
     */
    void removeConnection();
    void checkConnectionsStatus();
    void startNetwork();
    void startDiscovery();
    // for upnpn
    void upnpErrDis(QString msg);
    void upnpErrNet(QString msg);

    // spread messages

public slots:
    // test thread
    void process();
    void logDebug();
    void connectToServer();
    /**
     * @brief checkMyIdentificator
     */
    void checkMyIdentificator();
    /**
     * @brief Broadcast message to all connected peers
     * @param msg
     */
    void broadcastMsg(const QByteArray &msg);
    /**
     * @brief sendMessage
     * @param data for send
     * @param messageType type to compress
     */
    void sendMessage(const QByteArray &data, const QByteArray &msgType);
    void dfsMessageTmp(const Messages::DfsMessage &msg);
    /**
     * @brief sendMessageResponse from resolver
     * @param data
     * @param msgType
     * @param requestHash
     * @param receiver
     */
    void sendMessageResponse(const QByteArray &data, const QByteArray &msgType, const QByteArray &requestHash,
                             const SocketPair &receiver);
public slots:

    void sendGenesisBlock(Block prevBlock, QByteArray prevGenHash);

    //    void shareContract(Contract contract);
    void sendMessageTo(BigNumber recipientId, QByteArray message);

signals:
    void MessageReceived(const QByteArray &msg, const SocketPair &receiver);
    void sendMsg(const QByteArray &data, const SocketPair &socketData);

    void qmlNetworkStatus(bool status);
    void qmlServerError(bool serverError);
};

#endif // NETWORK_MANAGER_H
