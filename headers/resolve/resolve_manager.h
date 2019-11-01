#ifndef RESOLVE_MANAGER_H
#define RESOLVE_MANAGER_H

#ifndef NETWORK_MANAGER_DEF
#define NETWORK_MANAGER_DEF
class NetManager;
#include "headers/network/network_manager.h"
#endif
#ifndef RESOLVER_SERVICE_DEF
#define RESOLVER_SERVICE_DEF
class ResolverService;
#include "headers/resolve/resolver_service.h"
#endif
#ifndef NODE_MANAGER_DEF
#define NODE_MANAGER_DEF
class NodeManager;
#include "headers/managers/node_manager.h"
#endif

#include <QObject>
#include "headers/datastorage/blockchain.h"
#include "headers/datastorage/index/actorindex.h"
#include "headers/managers/tx_manager.h"
#include "dfs/controls/headers/dfs.h"

class ResolveManager : public QObject
{
    Q_OBJECT
private:
    QList<ResolverService *> resolvers;
    QMap<QByteArray, int> *requestResponseMap = new QMap<QByteArray, int>();
    QMap<QByteArray, int> *packageHandler = new QMap<QByteArray, int>();

private:
    ActorIndex *actorIndex;
    Blockchain *blockchain;
    NetManager *networkManager;
    TransactionManager *txManager;
    AccountController *accountControler;
    Dfs *dfs;
    NodeManager *node;

public:
    ResolveManager(ActorIndex *actorIndex, Blockchain *blockchain, NetManager *networkManager,
                   TransactionManager *txManager, AccountController *accountControler, Dfs *dfs,
                   QObject *parent = nullptr);
    ~ResolveManager();

    void setNode(NodeManager *value);

private:
    void connectSignals(ResolverService *resolver);
    void disconnectSignals(ResolverService *resolver);

    const QByteArray calcKeccak256(const QByteArray &msg) const;

private:
    QList<ResolverService *> getActive();
    QList<ResolverService *> getFinished();

signals:
    void finished();
    //    void coinRequest(BigNumber id, BigNumber amount);
    //    void sendMsg(const QByteArray &msg);
    void socketSendMsg(const QByteArray &serialized, const SocketPair &receiver);
public slots:
    void resolveMessage(const QByteArray &msg, const SocketPair &receiver);
    void setTask(QByteArray msg, const SocketPair &receiver);
    void registrateMsg(const QByteArray &data, const QByteArray &msgType);
    /**
     * @brief sendMessageResponse from resolver
     * @param data
     * @param msgType
     * @param requestHash
     * @param receiver
     */
    void sendMessageResponse(const QByteArray &data, const QByteArray &msgType, const QByteArray &requestHash,
                             const SocketPair &receiver);
    void taskFinished();
public slots:
    void process();
};

#endif // RESOLVE_MANAGER_H
