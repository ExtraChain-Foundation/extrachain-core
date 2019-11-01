#ifndef RESOLVERSERVICE_H
#define RESOLVERSERVICE_H

#ifndef RESOLVE_MANAGER_DEF
#define RESOLVE_MANAGER_DEF
class ResolveManager;
#include "headers/resolve/resolve_manager.h"
#endif
#ifndef NODE_MANAGER_DEF
#define NODE_MANAGER_DEF
class NodeManager;
#include "headers/managers/node_manager.h"
#endif

#ifndef ACTOR_INDEX_DEF
#define ACTOR_INDEX_DEF
class ActorIndex;
#include "headers/datastorage/index/actorindex.h"
#endif

#ifndef BLOCKCHAIN_DEF
#define BLOCKCHAIN_DEF
class Blockchain;
#include "headers/datastorage/blockchain.h"
#endif

#ifndef TX_MANAGER_DEF
#define TX_MANAGER_DEF
class TransactionManager;
#include "headers/managers/tx_manager.h"
#endif

#ifndef DFS_DEF
#define DFS_DEF
class Dfs;
#include "dfs/controls/headers/dfs.h"
#endif

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QMutex>

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/transaction.h"
#include "managers/account_controller.h"
#include "network/packages/service/all_messages.h"
#include "network/packages/base_message.h"
#include "network/packages/base_message_response.h"
#include "network/packages/service/response_messages.h"
#include "network/socket_pair.h"
#include "datastorage/genesis_block.h"
#include "dfs/packages/headers/all.h"

static QMutex handlerFileMutex;

/**
 * @brief The ResolverService class - the interlayer between Network packages
 * and system objects logic. The main idea of ResolverService is to detect message
 * type and deserialize it. There are package definition methods, and signals to
 * ResolveManager.
 */

class ResolverService : public QObject
{
    Q_OBJECT
private:
    NodeManager *node;
    ActorIndex *actorIndex;
    Blockchain *blockchain;
    Dfs *dfs;

private:
    bool active = false;

    QByteArray msg;
    QByteArray hash;
    SocketPair senderAddress;

    AccountController *ac;

    QMap<QByteArray, int> *requestResponseMap;

public:
    /**
     * @brief ResolverService
     * @param parent
     */
    ResolverService(QMap<QByteArray, int> *rrMap, QObject *parent = nullptr);
    /**
     * @brief ResolverService
     * @param actorIndex
     * @param parent
     */
    ResolverService(ActorIndex *actorIndex, QMap<QByteArray, int> *rrMap, QObject *parent = nullptr);
    /**
     * @brief ResolverService
     */
    ~ResolverService() override;
    /**
     * @brief isActive
     * @return
     */
    bool isActive() const;
    /**
     * @brief setTask
     * @param msg
     * @param receiver
     */
    void setTask(QByteArray msg, SocketPair receiver);

    void setNode(NodeManager *value);

    void setBlockchain(Blockchain *value);

    void setDfs(Dfs *value);

private:
    /**
     * @brief validate
     * @param block
     * @return
     */
    bool validateBlock(const Block &block);
    /**
     * @brief validate
     * @param tx
     * @return
     */
    bool validate(const Transaction &tx);
    /**
     * @brief validate
     * @param message
     * @return
     */
    bool validate(const Messages::IMessage &message);
    /**
     * @brief checkMsgType
     * @param msg
     * @return
     */
    QByteArray checkMsgType(const QByteArray &msg) const;
    /**
     * @brief calcHash
     * @param request
     * @return
     */
    QByteArray calcHash(const QByteArray &request) const;
    /**
     * @brief MessageIsNotValid
     * @param message
     * @return
     */
    bool MessageIsNotValid(const Messages::IMessage &message);
    /**
     * @brief addResponseHandler
     * @param message
     * @return
     */
    bool addResponseHandler(const QByteArray &message, const QByteArray &msgType);
    /**
     * @brief checkResponseHandler
     * @param message
     * @return
     */
    bool checkResponseHandler(const QByteArray &hash);
    /**
     * @brief Process recieved messages - detect package type and emit
     * corresponding signals
     * @param msg - serialized packages
     */
    void recieveMsg(const QByteArray &msgS, const SocketPair &receiver);
    void resolveDfsMessage(const QByteArray &data, const int &msgType, const SocketPair &receiver);
public slots:
    /**
     * @brief process
     * slot for threadpool
     * ready for work
     */
    void process();

signals:
    /**
     * @brief TaskFinished signal to resolver manager
     * the work have been finished you could kill me
     */
    void TaskFinished();
    /**
     * @brief responseReady to network manager
     * @param data
     * @param msgType
     * @param requestHash
     * @param receiver
     */
    //    void responseReady(const QByteArray &data, const QByteArray &msgType, const QByteArray &requestHash,
    //                       const SocketPair &receiver);
    // retranslate package to their owners class
    // new data signals
    void newDfsPack(const Messages::DfsMessage &msg);

    void newProfile(const QByteArray &msg);

    //    void newActor(const Actor<KeyPublic> &actor);

    //    void newBlock(const Block &block);
    void newGenesisBlock(const GenesisBlock &block);

    void newTx(const Transaction &tx);

    // request
    void getActor(const BigNumber &actorId, QByteArray reqHash, const SocketPair &receiver);

    void getTx(const SearchEnum::TxParam &param, const QByteArray &value, const SocketPair &receiver,
               const QByteArray &request);

    void getBlock(const SearchEnum::BlockParam &param, const QByteArray &value, const QByteArray &requestHash,
                  const SocketPair &receiver);

    void coinRequest(BigNumber id, BigNumber amount);

    void getActorsCount(const QByteArray &requestHash, const SocketPair &receiver);

    void getBlocksCount(const QByteArray &requestHash, const SocketPair &receiver);

    //    void dfsMessage(const QByteArray &data, const int &msgType, const SocketPair &receiver);
    // response
    void blockCount(const BigNumber &count);

    // signal for thread pool
    void finished();
};
#endif // RESOLVERSERVICE_H
