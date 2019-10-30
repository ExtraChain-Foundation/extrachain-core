#ifndef DFSNETMANAGER_H
#define DFSNETMANAGER_H

#include "headers/network/network_manager.h"
#include "dfs/packages/headers/all.h"

class DFSNetManager : public NetManager
{
    Q_OBJECT
    QList<SocketService *> socketsList;
    QMap<QByteArray, int> handler;
    quint16 serverPort;
    ServerService *serverService;

private:
    /**
     * @brief socketConnection
     * create connection for last append socket to the list
     */
    void socketConnection();
    void socketDisconnect(SocketService *connection);

public:
    DFSNetManager(AccountController *accountList, ActorIndex *actorIndex);
    ~DFSNetManager();

    NetManager *getNetManager();
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
};

#endif // DFSNETMANAGER_H
