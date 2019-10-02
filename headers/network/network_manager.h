#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "dfs/packages/headers/dfs_universal.h"
#include "network/packages/service/list_connections.h"
#include <QMap>
#include <QNetworkInterface>
#include <QObject>
#include <QtCore/QThread>
#include <QtNetwork/QNetworkAddressEntry>
#include <algorithm>

#include "datastorage/block.h"
#include "datastorage/blockchain.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "managers/thread_pool.h"
#include "network/discovery_service.h"
#include "network/resolver_service.h"
#include "network/server_service.h"
#include "network/socket_service.h"
#include "network/upnpconnection.h"
#include "network/socket_pair.h"

#include "network/packages/service/all_messages.h"
#include "network/packages/service/downloaddfsrequest.h"

/**
 * @brief The NetManager class
 * Creates Discovery, Resolver, Server and Sockets services
 */

class NetManager : public QObject
{
    Q_OBJECT
    const int maxValueTryConnections = 3;

private:
    bool reservedActorListUse = false;
    bool active = false;
    quint16 extPort;
    quint16 netPort;
    BigNumber maxBlockCount; // latest known block num in the blockchain
    QNetworkAddressEntry *local = nullptr;
    UPNPConnection *upnpDis;
    UPNPConnection *upnpNet;

    bool isDebug =
#ifdef QT_DEBUG
        true;
#else
        false;
#endif
    QString serverIp = "51.68.181.52;51.68.181.53";
    quint16 serverPort = isDebug ? 2221 : 2222;
    bool allowLocalServer = false;

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
            return responseCount >= Config::Net::NECESSARY_RESPONSE_COUNT && (responseCount % 2 != 0);
        }

        T resolveBestEntity()
        {
            // resolve best Entities from map
            QList<int> entries = responses.values();
            int maxCount = *std::max_element(entries.begin(), entries.end());

            // remove all Entities with entry count < max
            for (typename QMap<T, int>::iterator it = responses.begin(); it != responses.end();)
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
    QMap<QByteArray, GetCountHandler> getCountHandlers; // for getBlockCount && getActorCount
    QMap<QByteArray, GetEntityHandler<Actor<KeyPublic>>> getActorsHandlers;
    QMap<QByteArray, GetEntityHandler<Block>> getBlockHandlers;
    QMap<QByteArray, GetEntityHandler<Transaction>> getTxHandlers;
    QMap<QByteArray, GetEntityHandler<TxPair>> getTxPairHandlers;
    QMap<QByteArray, GetEntityHandler<BigNumber>> getReserveActorHandlers;

private:
    ActorIndex *actorIndex;
    AccountController *accounts;

    ResolverService *resolverService;
    //    DiscoveryService *discoveryService;
    ServerService *serverService;
    QList<SocketService *> connections;
    //    QHash<SocketPair, int> disconnectedSocketList;
    SocketPair checkConnection = SocketPair("0.0.0.0", 0, this);
    bool status_test_file = false;
    //#ifdef ETALONIUM_CONSOLE
    QList<BigNumber> reservedActorList;

    //    BigNumber deviceId;
    //#endif

#ifdef ETALONIUM_CLIENT
    QTcpSocket *socket_wer;
#endif

public:
    NetManager(AccountController *accountList, ActorIndex *actorIndex);
    ~NetManager();

    void showMessage(const QHostAddress &from, const QString &message);
    void sendMessageTest();

    void resolverMessage(const QHostAddress &from, const QString &message);

public:
    // void run() override;
    // int exec();
    // void quit();
    // bool isActive() const;

    //    DiscoveryService *getDiscoveryService();
    ServerService *getServerService();
    ResolverService *getResolverService();

    QList<SocketService *> getConnections() const;

signals:
    void finished();

    void creaTx();
    void downloadDfsResponse(Messages::DownloadDfsRequestData msg, QString senderIp);

private:
    /**
     * @brief Send message directly to the selected peer
     * @param msg
     * @param peerAddress
     */
    void sendMsgToPeer(Messages::IMessage &msg, QHostAddress peerAddress);
    void sendMsgToPeerPort(Messages::IMessage &msg, QHostAddress peerAddress, int port);

    /**
     * @brief Compares the conections list size and MINIMUM_PEERSs
     * @return true if there are enough peers in connections list
     */
    bool hasEnoughPeers() const;

    void findLocal();

    void restoreConnections(const QList<SocketPair> &socketList);

    void setupActorIndexConnections();
    void setupServerServiceConnections();
    void setupDiscoveryServiceConnections();
    void setupResolverServiceConnections();

    void signMessage(Messages::IMessage &message) const;
    QByteArray calcHash(Messages::IMessage &message) const;

private slots:

    void getNewConnectionList(QList<QByteArray> newConList);

    void createNewConnectionsFromList(const QByteArray &message);

    void checkConnectionsStatus();
    void startNetwork();
    void startDiscovery();
    void upnpErrDis(QString msg);
    void upnpErrNet(QString msg);

    // spread messages
    void handleNewActor(Actor<KeyPublic> actor, const QByteArray &requestHash, QHostAddress peerAddress);
    void handleNewBlock(Block block, QHostAddress peerAddress);
    void handleNewGenesisBlock(Block block, QHostAddress peerAddress);
    void handleNewTx(Transaction tx, QHostAddress peerAddress);

    void handleBlockApproved(BigNumber blockId, BigNumber approver, QHostAddress peerAddress);

    // request messages
    // processed in blockchain (because we don't need to block network thread)
    void handleGetActor(BigNumber actorId, QHostAddress peerAddress, QByteArray requestHash);
    void handleGetTx(SearchEnum::TxParam param, QByteArray value, QHostAddress peerAddress,
                     QByteArray requestHash);
    void handleGetTxPair(BigNumber sender, BigNumber receiver, QHostAddress peerAddress,
                         QByteArray requestHash);
    void handleGetBlock(SearchEnum::BlockParam param, QByteArray value, QHostAddress peerAddress,
                        QByteArray requestHash);
    void handleGetBlockCount(const QHostAddress &peerAddress, const QByteArray &requestHash);
    void handleGetActorCount(const QHostAddress &peerAddress, const QByteArray &requestHash);

    // response messages (waiting for NECESSARY_RESPONSE_COUNT responses)
    void handleGetActorResponse(Actor<KeyPublic> actor, QByteArray reqHash, QHostAddress peerAddress);
    void handleGetTxResponse(Transaction tx, QByteArray reqHash, QHostAddress peerAddress);
    void handleGetTxPairResponse(TxPair pair, QByteArray reqHash, QHostAddress peerAddress);
    void handleGetBlockResponse(Block block, QByteArray reqHash, QHostAddress peerAddress);
    void handleGetBlockCountResponse(BigNumber blockCount, QByteArray reqHash, QHostAddress peerAddress);
    void handleGetActorCountResponse(BigNumber actorCount, QByteArray reqHash, QHostAddress peerAddress);
    void handleReserveActorResponse(const BigNumber &actorId, const QByteArray &requestHash,
                                    const QString &peerAdress);
public slots:
    // test thread
    void process();
    void logDebug();
    void connectToServer();
    /**
     * @brief checkMyIdentificator
     */
    void checkMyIdentificator();
    /**
     * @brief Broadcast message to all connected peers
     * @param msg
     */
    void broadcastMsg(const QByteArray &msg);
    /**
     * @brief sendMessage
     * @param data for send
     * @param messageType type to compress
     */
    void sendMessage(const QByteArray &data, const QByteArray &messageType);

private slots:
    /**
     * @brief Creates new socket connection and adds it to connections
     * @param address
     * @param port
     */
    SocketService *addConnectionFromPair(QHostAddress address, quint16 port);
    /**
     * @brief addConnection
     * @param socketDescriptor
     */
    void addConnection(qint64 socketDescriptor);
    /**
     * @brief Remove connections from connection list
     */
    void removeConnection();
public slots:

    void sendProfile(PublicProfile profile);
    void reserveActor(const bool account);
    void retranslateMessages(const QByteArray &msg, QString peerAddress);
    void Verify(const QByteArray &block);
    /**
     * @brief init function
     * @return
     */

    void continueHandlingNewBlock(Block block);
    void continueHandlingNewActor(Actor<KeyPublic> actor);

    // send msgs
    void sendNewTx(Transaction tx);
    void sendNewContract(Contract contract);
    void sendNewBlock(Block block);
    void sendTxResponse(Transaction tx, TxParam param, QString value, QHostAddress peerAddress,
                        QByteArray requestHash);
    void sendTxPairResponse(TxPair pair, QHostAddress peerAddress, QByteArray requestHash);
    void sendBlockResponse(Block block, BlockParam param, QString value, QHostAddress peerAddress,
                           QByteArray requestHash);
    void sendBlockCountResponse(BigNumber blockCount, QHostAddress peerAddress, QByteArray requestHash);
    void sendActorCountResponse(BigNumber actorCount, QHostAddress peerAddress, QByteArray requestHash);

    // unique behavior (get block from temp file)
    void sendGenesisBlock(Block prevBlock, QByteArray prevGenHash);

    // requests for entities from other peers
    void sendGetActor(BigNumber actorId);
    void shareContract(Contract contract);
    void sendMessageTo(BigNumber recipientId, QByteArray message);
    void sendGetTx(SearchEnum::TxParam param, QString value);
    void sendGetBlock(SearchEnum::BlockParam param, QString value);
    void sendGetBlockCount();
    void sendGetActorCount();
    void sendGetTxPair(BigNumber sender, BigNumber receiver);

    void sendCompanyActor(QString peerAddress);
    void sendReserveActorRequest(QString peerAddress, QByteArray requestHash, int port);
    void sendConnectionList(Messages::EnableConnections sendConList, SocketService *addressant);

    void sendCoinRequest(BigNumber amount);
    void sendDfsPack(const Messages::DfsMessage &msg);
    void sendDfsMessageTo(Messages::DfsMessage dfs, QString peerAddress);
    void sendDfsRequest(const Messages::DfsRequest &msg);

    //    void sendDfsPackTo(Messages::DfsMessage dfs, QString peerAddress);
    void downloadAnswer(bool status, QByteArray msg, QString peerAddressst);

    void sendNewActor(Actor<KeyPublic> actor);

signals:
    void sendMsg(const QByteArray &data, const SocketPair &socketData);

    void qmlNetworkStatus(bool status);
    void qmlServerError(bool serverError);

    void newDfsPack(Messages::DfsMessage msg /*, QString senderId*/);
    void receiveProfile(PublicProfile profile);
    void downloadDfsRequest(QByteArray header, QString peerAdress);
    //    void downloadDfsResponse(DownloadDfsRequestData msg, QString senderIp);
    void getDfsRequest(Messages::DfsRequest msg, QString senderIp);
    void sendMediaDfs(QByteArray path, QList<QByteArray> list);
    void SendBlockExistence(const Block &block);
    // spread
    void CheckBlockExistence(Block block);
    void CheckActorExistence(Actor<KeyPublic> actor);

    void NewActor(Actor<KeyPublic> actor);
    void NewTx(Transaction tx);
    void coinRequest(BigNumber receiver, BigNumber amount);

    void BlockApproved(BigNumber blockId, BigNumber approver, QHostAddress peerAddress);

    // requests
    void GetTx(SearchEnum::TxParam param, QByteArray value, QHostAddress peerAddress, QByteArray requestHash);
    void GetTxPair(BigNumber sender, BigNumber receiver, QHostAddress peerAddress, QByteArray requestHash);
    void GetBlock(SearchEnum::BlockParam param, QByteArray value, QHostAddress peerAddress,
                  QByteArray requestHash);
    void GetBlockCount(QHostAddress peerAddress, QByteArray requestHash);
    void GetActorCount(QHostAddress peerAddress, QByteArray requestHash);

    // responses
    void AddBlock(Block block);
    void TxResponse(Transaction tx, QHostAddress peerAddress);
    void TxPairResponse(TxPair pair, QHostAddress peerAddress);
    void BlockCountResponse(BigNumber blockCount, QHostAddress peerAddress);
    void GetTxResponse(Transaction tx, SearchEnum::TxParam param);
    void GetBlockResponse(Block block, SearchEnum::BlockParam param);
    void GetActorResponse(Actor<KeyPublic> actor);

    // net&blockchain test
    void newTrans(Transaction &newTransact);
    void requestBlockCount();
    void requestActorCount();

    void nodeTxCreate(BigNumber receiver, BigNumber amount);

    // contracts
    void contractFirstTransaction(Contract &contract);
    void contractFinalTransaction(Contract &contract);

    void createActorWithId(BigNumber actorId, bool account);
};

#endif // NETWORK_MANAGER_H
