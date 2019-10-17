
#ifndef RESOLVE_MANAGER_H
#define RESOLVE_MANAGER_H

#include <QObject>
#include "headers/resolve/resolver_service.h"
#include "headers/network/network_manager.h"
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
    Dfs *dfs;

public:
    ResolveManager(ActorIndex *actorIndex, Blockchain *blockchain, NetManager *networkManager,
                   TransactionManager *txManager, Dfs *dfs, QObject *parent = nullptr);
    ~ResolveManager();

private:
    void connectSignals(ResolverService *resolver);
    void disconnectSignals(ResolverService *resolver);

    const QByteArray calcKeccak256(const QByteArray &msg) const;

private:
    QList<ResolverService *> getActive();
    QList<ResolverService *> getFinished();

signals:
    void finished();
    void coinRequest(BigNumber id, BigNumber amount);
    void sendMsg(const QByteArray &msg, const QByteArray &msgType);
public slots:
    void resolveMessage(const QByteArray &msg, const SocketPair &receiver);
    void setTask(QByteArray msg, const SocketPair &receiver);
    void registrateMsg(const QByteArray &msg, const QByteArray &msgType);
    void taskFinished();
public slots:
    void process();
};

#endif // RESOLVE_MANAGER_H
