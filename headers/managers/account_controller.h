#ifndef ACCOUNT_CONTROLLER_H
#define ACCOUNT_CONTROLLER_H

#include "utils/bignumber.h"
#include "datastorage/actor.h"
#include "datastorage/index/actorindex.h"
#include "enc/key_private.h"
#include <QDebug>
#include <QObject>
class Blockchain;
/**
 * @brief The AccountController class
 * One client can have several accounts, so AccountController is storing this accounts
 * and provides access to them.
 */

class AccountController : public QObject
{
    Q_OBJECT
private:
    // Current user, used in AccountController.
    int userNum = 0;
    Blockchain *blockchain;
    QList<Actor<KeyPrivate> *> accounts;
    ActorIndex *actorIndex;
    QMap<QByteArray, QByteArray> currentState;

public:
    AccountController(ActorIndex *actorIndex);
    QList<QByteArray> getAccountID();

public:
    /**
     * @brief Generates a new actor and adds it into accounts list
     * @return created actor
     */
    Actor<KeyPrivate> createActor(ActorType account, QByteArray hashLogin);
    //    Actor<KeyPrivate> createActorWithId(BigNumber id, bool account, bool contract = false);
    Actor<KeyPrivate> getActor(BigNumber id);
    /**
     * @brief Gets Actor by public key
     * @param pubkey - serialized public key
     * @return actor
     */
    Actor<KeyPrivate> getActorByPublicKey(QByteArray pubkey);
    Actor<KeyPrivate> getActor(int number);

    Actor<KeyPrivate> *getMainActor();
    /**
     * @brief Gets current active actor
     * @return actor
     */
    Actor<KeyPrivate> getCurrentActor();

    int getAccountCount();

    int getUserNum() const;
    void setUserNum(int value);
    QMap<QByteArray, QByteArray> getCurrentState() const;
    void setCurrentState(const QMap<QByteArray, QByteArray> &value);

    QList<Actor<KeyPrivate> *> getAccounts() const;
    void setAccounts(const QList<Actor<KeyPrivate> *> &value);

    ActorIndex *getActorIndex() const;
    void setActorIndex(ActorIndex *value);

    void setBlockchain(Blockchain *value);
    Blockchain *getBlockchain() const;
    QList<BigNumber> getListAccounts() const;

public slots:
    /**
     * @brief Loads actors from local disk to memory: QList accounts;
     */
    void loadActors(QByteArray id = "", QByteArrayList idList = {}, QByteArray hashLogin = "");
    /**
     * @brief Saves Private actor on local disk in serialized form
     * @param private actor
     */
    void savePrivateActor(Actor<KeyPrivate> actor, QByteArray hashLogin);
    //    void regNewUser(bool account);
    void clearAcc();
    void changeUserNum(QByteArray);
    void process();
signals:
    /**
     * @brief verifyActor
     * @param serialized private actor
     */
    void addActorInActorIndex(Actor<KeyPublic> actor);
    void verifyActor(Actor<KeyPublic> actor);
    //
    void sentActorId(BigNumber actorId);
    void loadWallets(QByteArray id, QByteArrayList idList);
    void updateTransactionListInModel();
    void newActorIsCreated(BigNumber id, bool isUser);
    void savePrivateProfile(QByteArray id);
    void finished();

    void initDfs();
    void editPrivateProfile(QByteArray id);
};
#endif // ACCOUNT_CONTROLLER_H
