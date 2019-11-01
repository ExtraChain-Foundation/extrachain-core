#ifndef DFSNETMANAGER_H
#define DFSNETMANAGER_H
#ifndef SOCKET_SERVICE_DEF
#define SOCKET_SERVICE_DEF
class SocketService;
#include "headers/network/socket_service.h"
#endif // SOCKET_SERVICE

#include "headers/network/network_manager.h"
#include "dfs/packages/headers/all.h"

class DFSNetManager : public NetManager
{
    Q_OBJECT
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

public:
    NetManager *getNetManager();
    void *MessageReceived(const QByteArray &msg, const SocketPair &receiver) override;
signals:
    void finished();
    void sendMsg(const QByteArray &message, const SocketPair &receiver);
    void newMessage(const QByteArray &message, const SocketPair &receiver);
public slots:
    void appendSocket(SocketService *socket);
    void newMsg(const QByteArray &message, const SocketPair &receiver);
    void send(const QByteArray &message, const QByteArray &msgType = Messages::DFS_MESSAGE,
              const SocketPair &receiver = SocketPair());
    void process();
private slots:
    void removeConnection();
    void checkMyIdentificator();
    void addConnection(qint64 socketDescriptor) override;
    void checkConnectionsStatus() override;
    void connectToServer(const quint16 &serverPort, QNetworkAddressEntry *local) override;
    SocketService *addConnectionFromPair(QHostAddress address, quint16 port) override;
};

#endif // DFSNETMANAGER_H
