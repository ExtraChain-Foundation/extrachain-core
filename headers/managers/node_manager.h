#ifndef NODE_MANAGER_H
#define NODE_MANAGER_H
#ifndef RESOLVE_MANAGER_DEF
#define RESOLVE_MANAGER_DEF
class ResolveManager;
#include "resolve/resolve_manager.h"
#endif
#include <QObject>
#include <QMap>
#include "network/network_manager.h"
#include "managers/tx_manager.h"
#include "managers/account_controller.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/blockchain.h"
#include "datastorage/block.h"
#include "datastorage/transaction.h"
#include "datastorage/actor.h"
#include "managers/thread_pool.h"
#include "dfs/controls/headers/dfs.h"
#include "managers/contract_manager.h"
#include "enc/crypt_manager.h"
#include "managers/sm_manager.h"
#include "dfs/managers/headers/dfsnetmanager.h"
#include "managers/chatmanager.h"
#include "profile/private_profile.h"
#include "dfs/controls/headers/subscribe_controller.h"
#include "headers/network/packages/service/message_types.h"

#ifdef ETALONIUM_CLIENT
#include "ui/ui_controller.h"
#include "headers/ui/notificationclient.h"
#include "managers/notification_manager.h"
#endif

#ifdef ETALONIUM_CONSOLE
#include "managers/console_manager.h"
#endif

using namespace DfsStruct;
class NodeManager : public QObject
{
    Q_OBJECT
private:
    // common object for
    bool fileMode = true;
    Dfs *dfs;
    ActorIndex *actorIndex;
    Blockchain *blockchain;
    NetManager *netManager;
    TransactionManager *txManager;
    AccountController *accController;
    SmartContractManager *smContractController;
    ChatManager *chatManager;
    ResolveManager *resolveManager;
    SubscribeController *subscribeController;
    PrivateProfile *prProfile;

    QByteArray idPrivateProfile;
    QByteArray hashLoginPrivateProfile;

#ifdef ETALONIUM_CLIENT
    UiController *uiController;
    WalletController *uiWallet;
    NotificationClient *notificationClient = nullptr;
    NotificationManager *notifyM;
#endif
    CryptManager *cryptManager;
    //    ContractManager *contractManager;

public:
    NodeManager();
    ~NodeManager();

public:
    void createCompanyActor(const QString &email, const QString &password);
    Blockchain *getBlockchain();
    NetManager *getNetManager();
    AccountController *getAccController() const;

    void getBlockchainFile();

    /**
     * @brief Create new transaction from current user
     * @param tx
     */
    Transaction createTransaction(Transaction tx);

    /**
     * @brief Shortcut for another createTransaction method
     * @param receiver - receiver address
     * @param amount - coin count
     */
    Transaction createTransaction(BigNumber receiver, BigNumber amount, BigNumber token = 0);

    Transaction createTransactionFrom(BigNumber sender, BigNumber receiver, BigNumber amount,
                                      BigNumber token = 0);

    int getClientList();

public:
    void coinResponse(BigNumber receiver, BigNumber amount, BigNumber plsr);

#ifdef ETALONIUM_CLIENT
    UiController *getUiController() const;
    void setNotificationClient(NotificationClient *newNtfCl);
#endif

    QByteArray getIdPrivateProfile() const;
    QByteArray getHashLoginPrivateProfile() const;

    ChatManager *getChatManager() const;

private:
    Actor<KeyPrivate> CreateExtracoin();
    void showMessage(QString from, QString message);
    /**
     * @brief Connect signals between NetManager and Blockchain
     */
    void connectResolveManager();
    void connectSmContractManager();
    void connectTxManager();
    void connectUi();
    void connectConsole();
    void connectContractManager();
    void connectBlockchain();
    //    void connectAccountController();
    void connectActorIndex();
    void dfsConnection();
    void connectSignals();
    //    void dfsConnection();
    /**
     * @brief Creates folders for work, if they not exist
     */
    void prepareFolders();

signals:
    void ready();
    void sendMsg(const QByteArray &data, const unsigned int &type);
    void InitNet(ActorIndex *actorChain, AccountController *accountList);
    void NewTx(Transaction tx);
    // created keys for chat
    void sendKey(QByteArray key);
    void sendPrivateKey(QByteArray prKey);
    // public:
    void sendActorToWallet(QList<QByteArray> list);
    void sendActorStateList(QMap<QByteArray, QByteArray> map);

    void sendActorIdSeva(bool status, BigNumber actorId);
    void saveProfile(Actor<KeyPrivate> *key, QByteArrayList profile);
    void profileToUi(QString actorId, Profile profile);
    void sendTransactionContract(Transaction tx);
    //    void addActorInActorIndex(Actor<KeyPublic> actor);
    void nodeEditPrivateProfile(QPair<QByteArray, QByteArray>, const QString &type, const QByteArray &Data,
                                const bool &reWrite);
    void loadInfoFromPrProfile(const QByteArray &hash, const QByteArray &idProfile, const QString &type);
    void savePrivateProfile(const QByteArray &hash, const QByteArray &id);
    void setCurrentIdNotifyM(const QByteArray id);
    void getAllActorsNode(QByteArray id, bool acc);
    void loadProfileForConsoleLogin(const QByteArray &login, const QByteArray &password);

private slots:
    void getAllActors();
    void getAllActorsTimerCall();
    void setIdPrivateProfile(QByteArray id);
    void setHashLoginPrivateProfile(QByteArray hash);
    void logOut();

    //    void makeContractFirstTransaction(Contract &contract);
    //    void makeContractFinalTransaction(Contract &contract);
public slots:
    void tempareSlotForActors();

    // test net & blockchain

    //    void CheckBlockCount(BigNumber blockCount, QHostAddress peerAddress);
    //    void makeFirstContractTransaction(Contract contract);
    void createNetManagerIdentificator();
    void dfscreateNetManagerIdentificator();
#ifdef ETALONIUM_CLIENT
    void sendTransactionFromUi(BigNumber reciever, BigNumber actor, BigNumber token);

private slots:
    void createWalletInUi();
    void updateWalletInUi();
    void updateWalletList();
    void updateAvailableWalletList();
    void updateRecentActivities();
    void changeWalletIdUi(BigNumber walletId);
    void addNewWallet();

#endif

#ifdef ETALONIUM_CONSOLE
public: // TODO
    void setConsoleManager(ConsoleManager *consoleManager)
    {
        this->consoleManager = consoleManager;
    }

    auto &requestCoinQueue()
    {
        return m_requestCoinQueue;
    }

    void setListenCoinRequest(bool listenCoinRequest)
    {
        m_listenCoinRequest = listenCoinRequest;
    }

    bool listenCoinRequest()
    {
        return m_listenCoinRequest;
    }

    void sendCoinRequest(BigNumber receiver, BigNumber amount)
    {
        qInfo().noquote() << "Sending" << Transaction::amountToVisible(amount) << "coins to"
                          << receiver.toByteArray();
        createTransaction(receiver, amount, 0);
    }

private:
    ConsoleManager *consoleManager;
    QList<std::tuple<BigNumber, BigNumber, BigNumber>> m_requestCoinQueue;
    bool m_listenCoinRequest = false;
#endif
};
#endif // NODE_MANAGER_H
