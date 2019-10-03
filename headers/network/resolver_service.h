#ifndef RESOLVERSERVICE_H
#define RESOLVERSERVICE_H

#include <QHostAddress>
#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QMutex>

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/transaction.h"
#include "network/packages/service/all_messages.h"

static QMutex handlerFileMutex;

/**
 * @brief The ResolverService class - the interlayer between Network packages
 * and Blockchain logic. The main idea of ResolverService is to detect message
 * type and deserialize it. There are package definition methods, and signals to
 * NetManager.
 */

class ResolverService : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief ResolverService
     * @param parent
     */
    ResolverService(QObject *parent = nullptr);
    /**
     * @brief ResolverService
     * @param actorIndex
     * @param parent
     */
    ResolverService(ActorIndex *actorIndex, QObject *parent = nullptr);
    /**
     * @brief ResolverService
     */
    ~ResolverService() override;

private:
    bool active = false;
    ActorIndex *actorIndex;

    //    QMap<QByteArray, short> handlerList;

private:
    /**
     * @brief validate
     * @param block
     * @return
     */
    bool validate(const Block &block);
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
     * @brief universalHandler
     * @param msg
     * @param msgType
     */
    bool universalHandler(const Messages::IMessage &msg, const QByteArray &msgType);
    /**
     * @brief checkMsgCount
     * @param msg
     * @param msgType
     * @return
     */
    bool checkMsgCount(const Messages::IMessage &msg, const QByteArray &msgType);

public slots:
    // slot for threadpool
    /**
     * @brief process
     */
    void process()
    {
    }

    //
    /**
     * @brief Process recieved messages - detect package type and emit
     * corresponding signals
     * @param msg - serialized packages
     */
    void recieveMsg(const QByteArray &msgS, const QString &peerAddressst, const int port);

signals:

    /**
     * @brief secondWave
     * NetManager::connect(resolverService, &ResolverService::secondWave, this,
     * &NetManager::broadcastMsg);
     * @param msg
     */
    void secondWave(const QByteArray &msg);
    // retranslate package to their owners class
    void newDfsPack(const Messages::DfsMessage &msg);

    void receiveProfile(const PublicProfile &msg);

    void newActor(const Actor<KeyPublic> &actor);

    void newBlock(const Block &block);

    void newTx(const Transaction &tx);
    //    void createConnectionsList(const QByteArray &message);

    //    void SendGetActor(BigNumber actorId);
    //    void sendMediaData(QByteArray path, QList<QByteArray> headersList);

    //    void getNewConnectionList(QList<QByteArray>);

    //    // broadcast signal
    //    void secondWavesMsg(Messages::DfsMessage dfs, QByteArray text);
    //    void secondWavesRaw(Messages::DfsMessage dfs, QByteArray text);
    //    void contractFromNetwork(const Contract &contract);

    //    // dfs
    //    void getNewDfs(Messages::DfsMessage msg /*, QString senderId*/);
    //    void getDfsRequest(Messages::DfsRequest msg, QString senderIp);
    //    void downloadDfsResponse(Messages::DownloadDfsRequestData msg, QString senderIp);
    //    void downloadRequest(QByteArray msg, QString peerAddressst);
    //    // broadcast
    //    void broadcast(const QByteArray &msg, QString peerAdress);

    //    // spread signals
    //    void reserveActor(QString peerAddressst, QByteArray requestHash, int port);
    //    void createActorWithId(BigNumber id, bool accountStatu);
    //    void NewActor(Actor<KeyPublic> actor, QHostAddress peerAddress);
    //    void reserveActorResponse(const BigNumber &id, const QByteArray &hash, const QString &peerAddress);
    //    void NewBlock(Block block, QHostAddress peerAddress);
    //    void NewGenesisBlock(Block block, QHostAddress peerAddress);
    //    void CoinRequest(BigNumber sender, BigNumber amount);
    //    void NewTx(Transaction tx, QHostAddress peerAddress);
    //    void BlockApproved(BigNumber blockId, BigNumber approver, QHostAddress peerAddress);
    //    //    void MergedBlock(Block first, Block second, Block result, QByteArray dsig,
    //    //                     QHostAddress peerAddress);

    //    // request signals
    //    void VerifyActor(Actor<KeyPublic> actor, QHostAddress peerAddress);
    //    void GetActor(BigNumber actorId, QHostAddress peerAddress, QByteArray requestHash);
    //    void GetTx(SearchEnum::TxParam param, QByteArray value, QHostAddress peerAddress, QByteArray
    //    requestHash);

    //    void GetTxPair(BigNumber sender, BigNumber receiver, QHostAddress peerAddress, QByteArray
    //    requestHash);

    //    void GetBlock(SearchEnum::BlockParam param, QByteArray value, QHostAddress peerAddress,
    //                  QByteArray requestHash);
    //    void GetActorCount(QHostAddress peerAddress, QByteArray requestHash);
    //    void GetBlockCount(QHostAddress peerAddress, QByteArray requestHash);

    //    // response signals
    //    void VerifyActorResponse(Actor<KeyPublic> actor, bool verified, QHostAddress peerAddress);
    //    void GetActorResponse(Actor<KeyPublic> actor, QByteArray reqHash, QHostAddress peerAddress);
    //    void GetTxResponse(Transaction tx, QByteArray reqHash, QHostAddress peerAddress);
    //    void GetTxPairResponse(TxPair pair, QByteArray reqHash, QHostAddress peerAddress);
    //    void GetBlockResponse(Block block, QByteArray reqHash, QHostAddress peerAddress);
    //    void GetBlockCountResponse(BigNumber blockCount, QByteArray reqHash, QHostAddress peerAddress);
    //    void GetActorCountResponse(BigNumber actorCount, QByteArray reqHash, QHostAddress peerAddress);
    //    void ReceiveProfile(PublicProfile profile);
    // thread pool
    void finished();
};
#endif // RESOLVERSERVICE_H
