#ifndef DFSNETMANAGER_H
#define DFSNETMANAGER_H
#ifndef SOCKET_SERVICE_DEF
#define SOCKET_SERVICE_DEF
class SocketService;
#include "network/socket_service.h"
#endif // SOCKET_SERVICE

#include "network/network_manager.h"
#include "dfs/packages/headers/all.h"
#include "resolve/dfs_resolver_service.h"
#include "utils/utils.h"
class Dfs;
class DFSNetManager : public NetManager
{
    Q_OBJECT
private:
    Dfs *dfs;
    DFSResolverService *uResolver;
    QList<DFSResolverService *> dfsResolvers;
    QList<SocketService *> socketsList;
    QMap<QByteArray, int> handler;
    quint16 serverPort;
    ServerService *serverService;

public:
    DFSNetManager(AccountController *accountList, ActorIndex *actorIndex);
    ~DFSNetManager() override;

private:
    /**
     * @brief socketConnection
     * create connection for last append socket to the list
     */
    void socketConnection();
    void socketDisconnect(SocketService *connection);
    void startNetwork() override;
    void setupServerServiceConnections() override;
    //    bool checkMsgCount(const QByteArray &msg, QMap<QByteArray, int> &handler) override;
    void connectResolver(DFSResolverService *resolver);
    void disconnectResolver(DFSResolverService *resolver);

public:
    NetManager *getNetManager();
    void *MessageReceived(const QByteArray &msg, const SocketPair &receiver) override;
    void send(const QByteArray &message, const QByteArray &msgType = Messages::DFS_MESSAGE,
              const SocketPair &receiver = SocketPair());

    void setDfs(Dfs *value);
    bool isLoading(const QString &fileName);

signals:
    void newMessage(Network::DataStruct data);
    void finished();
    //    void sendMsg(const QByteArray &message, const SocketPair &receiver);
    //    void newMessage(const QByteArray &message, const SocketPair &receiver);
public slots:
    void appendSocket(SocketService *socket);
    //    void newMsg(const QByteArray &message, const SocketPair &receiver);
    void process();
    void startDFSNetwork();
    void uiReconnect();
    void titleArrived(Network::DataStruct ds);
    void removeResolver();

private slots:
    void removeConnection();
    void checkMyIdentificator();
    void addConnection(qint64 socketDescriptor) override;
    void checkConnectionsStatus() override;
    void connectToServer(const quint16 &serverPort, QNetworkAddressEntry *local) override;
    SocketService *addConnectionFromPair(QHostAddress address, quint16 port) override;
};

#endif // DFSNETMANAGER_H
