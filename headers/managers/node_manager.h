#ifndef NODE_MANAGER_H
#define NODE_MANAGER_H

#include <QObject>
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
#include "crypt/crypt_manager.h"
#include "managers/sm_manager.h"

#include "resolve/resolve_manager.h"

#include "profile/private_profile.h"

#ifdef ETALONIUM_CLIENT
#include "ui/ui_controller.h"
#endif

using namespace based_dfs_struct;
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

    ResolveManager *resolveManager;

    PrivateProfile *prProfile;
    QByteArray idPrivateProfile;
    QByteArray hashLoginPrivateProfile;
#ifdef ETALONIUM_CLIENT
    UiController *uiController;
    WalletController *uiWallet;

#endif
    CryptManager *cryptManager;
    ContractManager *contractManager;

public:
    NodeManager();
    ~NodeManager();

public:
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
    int getClientList();
    void updateActors();

#ifdef ETALONIUM_CLIENT
    UiController *getUiController() const;
#endif

    QByteArray getIdPrivateProfile() const;

    QByteArray getHashLoginPrivateProfile() const;

private:
    Actor<KeyPrivate> CreateExtracoin();
    void showMessage(QString from, QString message);
    /**
     * @brief Connect signals between NetManager and Blockchain
     */
    void connectResolveManager();
    void connectSmContractManager();
    void connectNetManager();
    void connectTxManager();
    void connectUi();
    void connectContractManager();
    void connectBlockchain();
    void connectAccountController();
    void connectActorIndex();
    void dfsConnection();
    void connectSignals();
    //    void dfsConnection();
    /**
     * @brief Creates folders for work, if they not exist
     */
    void prepareFolders();

signals:

    void InitNet(ActorIndex *actorChain, AccountController *accountList);
    void NewTx(Transaction tx);
    // created keys for chat
    void sendKey(QByteArray key);
    void sendPrivateKey(QByteArray prKey);
    // public:
    void sendActorToWallet(QList<QByteArray> list);
    void sendActorStateList(QMap<QByteArray, QByteArray> map);

    void sendActorIdSeva(bool status, BigNumber actorId);
    void requestProfile(QString actorId);
    void saveProfile(Actor<KeyPrivate> *key, Profile profile);
    void profileToUi(QString actorId, Profile profile);
    void sendTransactionContract(Transaction tx);
    void addActorInActorIndex(Actor<KeyPublic> actor);
    void editPrivateProfile(QByteArray hashLogin, QByteArray idProfile, QByteArray id);
private slots:
    void setIdPrivateProfile(QByteArray id);
    void setHashLoginPrivateProfile(QByteArray hash);
    void createNewActor(QByteArray hash, bool accountStatus);

    void makeContractFirstTransaction(Contract &contract);
    void makeContractFinalTransaction(Contract &contract);
public slots:

    void tempareSlotForActors();
    void coinResponse(BigNumber receiver, BigNumber amount);

    // test net & blockchain

    void CheckBlockCount(BigNumber blockCount, QHostAddress peerAddress);
    void makeFirstContractTransaction(Contract contract);
    void createNetManagerIdentificator();
#ifdef ETALONIUM_CLIENT
    void sendTransactionFromUi(BigNumber reciever, BigNumber actor, BigNumber token);

private slots:
    void createWalletInUi();
    void updateWalletInUi();
    void updateWalletList();
    void updateAvailableWalletList();
    void updateRecentActivities();
    void changeWalletIdUi(BigNumber walletId);

#endif
};
#endif // NODE_MANAGER_H
