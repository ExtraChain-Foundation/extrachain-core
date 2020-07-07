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

#ifdef ECONSOLE
static const short DFS_RESOLVERS_POOL_SIZE = 10;
#endif

#ifdef ECLIENT
static const short DFS_RESOLVERS_POOL_SIZE = 5;
#endif

class Dfs;
class DFSNetManager : public NetManager
{
    Q_OBJECT
private:
    std::queue<Network::DataStruct> titleVector;
    Dfs *dfs;
    DFSResolverService *uResolver;
    QList<DFSResolverService *> dfsResolvers;
    QMap<QByteArray, int> handler;
    ServerService *serverService;

public:
    DFSNetManager(AccountController *accountList, ActorIndex *actorIndex);
    ~DFSNetManager() override;

private:
    /**
     * @brief socketConnectionf
     * create connection for last append socket to the list
     */
    void socketConnection();
    void socketDisconnect(SocketService *connection);
    void connectResolver(DFSResolverService *resolver);
    void disconnectResolver(DFSResolverService *resolver);
    void createDFSResolver(Network::DataStruct ds);

public:
    NetManager *getNetManager();
    void *MessageReceived(const QByteArray &msg, const SocketPair &receiver) override;
    void setDfs(Dfs *value);
    bool isLoading(const QString &fileName);

    QList<DFSResolverService *> getDfsResolvers() const;

signals:
    void newMessage(Network::DataStruct data);
    void finished();
    //    void sendMsg(const QByteArray &message, const SocketPair &receiver);
    //    void newMessage(const QByteArray &message, const SocketPair &receiver);

public slots:
    void appendSocket(SocketService *socket);
    //    void newMsg(const QByteArray &message, const SocketPafir &receiver);
    void process();
    void startDFSNetwork();
    void uiReconnect();
    void titleArrived(Network::DataStruct ds);
    void removeResolver(DFSResolverService::FinishStatus status);

private slots:
    void removeConnection();
    void checkMyIdentificator();
    void addConnection(qint64 socketDescriptor) override;
    void checkConnectionsStatus() override;
    SocketService *addConnectionFromPair(QHostAddress address, quint16 port) override;
};

#endif // DFSNETMANAGER_H
