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

#include "managers/chatmanager.h"
class ChatManager;
static const short ResolverServicePoolMaxSize = 30;

#include <QObject>
//#include <QQueue>
#include <queue>
#include <vector>
#include "headers/datastorage/blockchain.h"
#include "headers/datastorage/index/actorindex.h"
#include "headers/managers/tx_manager.h"
#include "dfs/controls/headers/dfs.h"
struct DataStruct
{
    QByteArray msg;
    SocketPair receiver;
};

class ResolveManager : public QObject
{
    Q_OBJECT

private:
    QTimer *loadChecker;
    QMap<QByteArray, QString> *downloadingFileList = new QMap<QByteArray, QString>();
    QMap<QByteArray, std::vector<bool>> *dataCheckers;
private slots:
    void checkStatus();

private:
    std::queue<DataStruct> unprocessed;
    QList<ResolverService *> resolvers;
    QMap<QByteArray, int> *requestResponseMap = new QMap<QByteArray, int>();
    QMap<QByteArray, QFile *> *listFile = new QMap<QByteArray, QFile *>();
    QMap<QString, QByteArray> *fileMap = new QMap<QString, QByteArray>();
    QMap<QByteArray, unsigned long> *pckgCounter = new QMap<QByteArray, unsigned long>();

private:
    ActorIndex *actorIndex;
    Blockchain *blockchain;
    NetManager *networkManager;
    TransactionManager *txManager;
    AccountController *accountControler;
    Dfs *dfs;
    NodeManager *node;
    ChatManager *chatManager;

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
    /**
     * @brief createNewResolver
     * @param task
     */
    void createNewResolver(const DataStruct &task);

private:
    QList<ResolverService *> getActive();
    QList<ResolverService *> getFinished();
    bool popUnprocces();

public:
    bool setTask(QByteArray msg, const SocketPair &receiver);
    void setChatManager(ChatManager *value);

    QMap<QByteArray, std::vector<bool>> *getDataCheckers() const;

    QMap<QByteArray, unsigned long> *getPckgCounter() const;

signals:
    void finished();
    //    void coinRequest(BigNumber id, BigNumber amount);
    //    void sendMsg(const QByteArray &msg);
    void socketSendMsg(const QByteArray &serialized, const SocketPair &receiver);
public slots:
    void restartLoadChecker();
    //    void resolveMessage(const QByteArray &msg, const SocketPair &receiver);
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
