#ifndef LOCAL_MANAGER_H
#define LOCAL_MANAGER_H

#include <QObject>
#include <QtCore/QThread>
#include <QMap>
#include <algorithm>
#include <QtNetwork/QNetworkInterface>
#include <QtNetwork/QNetworkAddressEntry>

#include "extracoin/headers/datastorage/block.h"
#include "extracoin/headers/datastorage/blockchain.h"
#include "extracoin/headers/datastorage/index/actorindex.h"
#include "extracoin/headers/managers/account_controller.h"
#include "extracoin/headers/network/upnpconnection.h"
#include "extracoin/headers/network/discovery_service.h"
#include "extracoin/headers/network/server_service.h"
#include "extracoin/headers/network/socket_service.h"
#include "extracoin/headers/network/resolver_service.h"

#include "extracoin/headers/network/packages/service/all_messages.h"

/**
 * @brief The LocalManager class
 * Creates Discovery, Resolver, Server and Sockets services
 */
class LocalManager : public QThread
{
    Q_OBJECT
private:
    bool active = false;
    quint16 extPort = 1515;
    quint16 netPort = 1616;
    BigNumber maxBlockCount; // latest known block num in the blockchain
    QNetworkAddressEntry *local = nullptr;
    UPNPConnection *upnp;

    // virtual
    struct ResponseHandler
    {
    protected:
        int responseCount;
        virtual ~ResponseHandler()
        {
        }

    public:
        ResponseHandler()
            : responseCount(0)
        {
        }

    protected:
        void incrementResponseCount()
        {
            this->responseCount++;
        }

    public:
        virtual bool canProcess() = 0;

        int getResponseCount() const
        {
            return responseCount;
        }
    };

    struct GetCountHandler : public ResponseHandler
    {
    private:
        int responsesToProcess;
        BigNumber searchedValue = 0;

    public:
        GetCountHandler()
            : ResponseHandler()
            , responsesToProcess(Config::Net::NECESSARY_RESPONSE_COUNT)
        {
        }

        void addResponse(const BigNumber &value)
        {
            // find max value from all responses
            if (value > searchedValue)
            {
                searchedValue = value;
            }
            incrementResponseCount();
        }

        bool canProcess() override
        {
            return responseCount >= responsesToProcess;
        }

        BigNumber getSearchedValue() const
        {
            return searchedValue;
        }

        int getResponsesToProcess() const
        {
            return responsesToProcess;
        }

        QString toString() const
        {
            return QString("GetRequest[responseCount:%2, responsesToProcess:%3")
                .arg(responseCount, responsesToProcess);
        }
    };

    template <typename T>
    struct GetEntityHandler : public ResponseHandler
    {
    private:
        QMap<T, int> responses; // entity -> mentions count
    public:
        GetEntityHandler()
            : ResponseHandler()
        {
            responses.clear();
        }

        void addResponse(const T &response)
        {
            int mentionsCount = responses[response];
            responses.insert(response, ++mentionsCount);
            incrementResponseCount();
        }

        int getResponsesByEntity(const T &entity)
        {
            return responses[entity];
        }

        bool canProcess() override
        {
            // if number of responses is odd and more than Nessessary response count
            return responseCount >= Config::Net::NECESSARY_RESPONSE_COUNT
                && (responseCount % 2 != 0);
        }

        T resolveBestEntity()
        {
            // resolve best Entities from map
            QList<int> entries = responses.values();
            int maxCount = *std::max_element(entries.begin(), entries.end());

            // remove all Entities with entry count < max
            for (typename QMap<T, int>::iterator it = responses.begin();
                 it != responses.end();)
            {
                if (it.value() < maxCount)
                {
                    it = responses.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            // if we have one Entity with max entry count - it is the best entity
            if (responses.count() == 1)
            {
                return responses.keys().first();
            }

            // can't resolve best entities (there are 2 or more entities with equal
            // mentions count)
            return T();
        }
    };

    // Get response handlers
    QMap<QByteArray, GetCountHandler>
        getCountHandlers; // for getBlockCount && getActorCount
    QMap<QByteArray, GetEntityHandler<Actor<KeyPublic>>> getActorsHandlers;
    QMap<QByteArray, GetEntityHandler<Block>> getBlockHandlers;
    QMap<QByteArray, GetEntityHandler<Transaction>> getTxHandlers;
    QMap<QByteArray, GetEntityHandler<TxPair>> getTxPairHandlers;

private:
    ActorIndex *actorIndex;
    AccountController *accounts;

private:
    ResolverService *resolverService;
    DiscoveryService *discoveryService;
    ServerService *serverService;
    QList<SocketService *> connections;

public:
    LocalManager(quint16 externalPort = 1515, quint16 networkPort = 1616);
    ~LocalManager() override;

public:
    void run() override;
    int exec();
    void quit();
    bool isActive() const;

private:
    /**
     * @brief Broadcast message to all connected peers
     * @param msg
     */
    void broadcastMsg(Messages::IMessage &msg);
    /**
     * @brief Send message directly to the selected peer
     * @param msg
     * @param peerAddress
     */
    void sendMsgToPeer(Messages::IMessage &msg, QHostAddress peerAddress);

    /**
     * @brief Compares the conections list size and MINIMUM_PEERSs
     * @return true if there are enough peers in connections list
     */
    bool hasEnoughPeers() const;

    void findLocal();

    void setupActorIndexConnections();
    void setupDiscoveryServiceConnections();
    void setupResolverServiceConnections();

    void signMessage(Messages::IMessage &message) const;
    QByteArray calcHash(Messages::IMessage &message) const;

private slots:
    void startNetwork();
    void upnpErr(QString msg) const;

    // spread messages
    void handleNewActor(Actor<KeyPublic> actor, QHostAddress peerAddress);
    void handleNewBlock(Block block, QHostAddress peerAddress);
    void handleNewGenesisBlock(Block block, QHostAddress peerAddress);
    void handleNewTx(Transaction tx, QHostAddress peerAddress);

    void handleBlockApproved(BigNumber blockId, BigNumber approver,
                             QHostAddress peerAddress);
    void handleMergedBlock(Block first, Block second, Block result, QByteArray dsig,
                           QHostAddress peerAddress);

    // request messages
    // processed in blockchain (because we don't need to block network thread)
    void handleGetActor(BigNumber actorId, QHostAddress peerAddress,
                        QByteArray requestHash);
    void handleGetTx(SearchEnum::TxParam param, QByteArray value,
                     QHostAddress peerAddress, QByteArray requestHash);
    void handleGetTxPair(BigNumber sender, BigNumber receiver,
                         QHostAddress peerAddress, QByteArray requestHash);
    void handleGetBlock(SearchEnum::BlockParam param, QByteArray value,
                        QHostAddress peerAddress, QByteArray requestHash);
    void handleGetBlockCount(QHostAddress peerAddress, QByteArray requestHash);

    // response messages (waiting for NECESSARY_RESPONSE_COUNT responses)
    void handleGetActorResponse(Actor<KeyPublic> actor, QByteArray reqHash,
                                QHostAddress peerAddress);
    void handleGetTxResponse(Transaction tx, QByteArray reqHash,
                             QHostAddress peerAddress);
    void handleGetTxPairResponse(TxPair pair, QByteArray reqHash,
                                 QHostAddress peerAddress);
    void handleGetBlockResponse(Block block, QByteArray reqHash,
                                QHostAddress peerAddress);
    void handleGetBlockCountResponse(BigNumber blockCount, QByteArray reqHash,
                                     QHostAddress peerAddress);

private slots:
    /**
     * @brief Creates new socket connection and adds it to connections
     * @param address
     * @param port
     */
    void addConnection(QString address, quint16 port);

    /**
     * @brief Remove connections from connection list
     */
    void removeConnection();
public slots:
    void Verify(const QByteArray &block);
    /**
     * @brief init function
     * @return
     */
    void init(ActorIndex *actorChain, AccountController *accountList);

    void continueHandlingNewBlock(Block block);
    void continueHandlingNewActor(Actor<KeyPublic> actor);

    // send msgs
    void sendNewTx(Transaction tx);
    void sendNewBlock(Block block);
    void sendTxResponse(Transaction tx, TxParam param, QString value,
                        QHostAddress peerAddress, QByteArray requestHash);
    void sendTxPairResponse(TxPair pair, QHostAddress peerAddress,
                            QByteArray requestHash);
    void sendBlockResponse(Block block, BlockParam param, QString value,
                           QHostAddress peerAddress, QByteArray requestHash);
    void sendBlockCountResponse(BigNumber blockCount, QHostAddress peerAddress,
                                QByteArray requestHash);
    void sendMergedBlock(Block firstBlock, Block secondBlock, Block resultBlock);
    // unique behavior (get block from temp file)
    void sendGenesisBlock(Block prevBlock, QByteArray prevGenHash);

    // requests for entities from other peers
    void sendGetActor(BigNumber actorId);
    void sendGetTx(SearchEnum::TxParam param, QString value);
    void sendGetBlock(SearchEnum::BlockParam param, QString value);
    void sendGetBlockCount();
    void sendGetTxPair(BigNumber sender, BigNumber receiver);

    void sendNewActor(Actor<KeyPublic> actor);

signals:
    void SendBlockExistence(const Block &block);
    // spread
    void CheckBlockExistence(Block block);
    void CheckActorExistence(Actor<KeyPublic> actor);

    void NewActor(Actor<KeyPublic> actor);
    void NewTx(Transaction tx);

    void BlockApproved(BigNumber blockId, BigNumber approver,
                       QHostAddress peerAddress);
    void HandleMergedBlock(Block first, Block second, Block result, QByteArray dsig,
                           QHostAddress peerAddress);

    // requests
    void GetTx(SearchEnum::TxParam param, QByteArray value, QHostAddress peerAddress,
               QByteArray requestHash);
    void GetTxPair(BigNumber sender, BigNumber receiver, QHostAddress peerAddress,
                   QByteArray requestHash);
    void GetBlock(SearchEnum::BlockParam param, QByteArray value,
                  QHostAddress peerAddress, QByteArray requestHash);
    void GetBlockCount(QHostAddress peerAddress, QByteArray requestHash);

    // responses
    void AddBlock(Block block);
    void TxResponse(Transaction tx, QHostAddress peerAddress);
    void TxPairResponse(TxPair pair, QHostAddress peerAddress);
    void BlockCountResponse(BigNumber blockCount, QHostAddress peerAddress);
    void GetTxResponse(Transaction tx, SearchEnum::TxParam param);
    void GetBlockResponse(Block block, SearchEnum::BlockParam param);
    void GetActorResponse(Actor<KeyPublic> actor);
};

#endif // NETWORK_MANAGER_H
