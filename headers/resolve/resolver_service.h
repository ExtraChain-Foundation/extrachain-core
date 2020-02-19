#ifndef RESOLVERSERVICE_H
#define RESOLVERSERVICE_H

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QMutex>
#include <QTimer>
#include <QMap>
#include <vector>
#include <queue>

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/transaction.h"
#include "network/packages/service/all_messages.h"
#include "network/packages/base_message.h"
#include "network/packages/base_message_response.h"
#include "network/socket_pair.h"
#include "datastorage/genesis_block.h"
#include "dfs/packages/headers/all.h"
#include "headers/network/packages/service/message_types.h"

static QMutex handlerFileMutex;

/**
 * @brief The ResolverService class - the interlayer between Network packages
 * and system objects logic. The main idea of ResolverService is to detect message
 * type and deserialize it. There are package definition methods, and signals to
 * ResolveManager.
 */
class AccountController;
class ResolveManager;
class NodeManager;
class ActorIndex;
class Blockchain;
class TransactionManager;
class ChatManager;
using namespace Resolver;
class ResolverService : public QObject
{
    Q_OBJECT
private:
    NodeManager *node;
    ActorIndex *actorIndex;
    Blockchain *blockchain;
    ChatManager *chatManager;

private:
    Resolver::Type type = Resolver::Type::GENERAL;
    Lifetime lifetime = Lifetime::SHORT;

private:
    QTimer *reloadTimer = nullptr;
    //    QByteArray tag;
    std::vector<bool> dataChecker;
    //    QString path;
    QFile file;
    DistFileSystem::TitleMessage title;

private:
    bool active = false;
    QByteArray msg;
    QByteArray hash;
    SocketPair receiver;

    AccountController *ac;
    ResolveManager *resolveManager;

public:
    /**
     * @brief ResolverService
     * @param actorIndex
     * @param parent
     */
    ResolverService(Type type, Lifetime lifetime, ActorIndex *actorIndex, ResolveManager *resolveManager,
                    QObject *parent = nullptr);
    /**
     * @brief ResolverService
     */
    ~ResolverService() override;

private:
    void finishWork();

public:
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

    void setChatManager(ChatManager *value);

    void setResolveManager(ResolveManager *value);

    Resolver::Type getType() const;
    void setType(const Resolver::Type &value);

    Lifetime getLifetime() const;

    //    DFS::titleMessage getTitle() const;

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
    bool validate(const Messages::BaseMessage &message);
    /**
     * @brief checkMsgType
     * @param msg
     * @return
     */
    //    QByteArray checkMsgType(const QByteArray &msg) const;
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
    bool MessageIsNotValid(const Messages::BaseMessage &message);
    /**
     * @brief addResponseHandler
     * @param message
     * @return
     */
    bool addResponseHandler(const QByteArray &message, const unsigned int &msgType);
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
    void resolveTask();
    void resolveGeneralTask();
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
    void newProfile(const QByteArray &msg);

    //    void newActor(const Actor<KeyPublic> &actor);

    //    void newBlock(const Block &block);
    void newGenesisBlock(GenesisBlock block);

    void newTx(const Transaction &tx);

    // request
    void getActor(const BigNumber &actorId, QByteArray reqHash, const SocketPair &receiver);

    void handleGetAllActor(QByteArray reqHash, const SocketPair &receiver);

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
