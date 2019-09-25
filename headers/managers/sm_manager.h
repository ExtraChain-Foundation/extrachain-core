#ifndef SM_CONTROLLER_H
#define SM_CONTROLLER_H

#include <QObject>
#include <QDebug>

#include "utils/bignumber.h"
#include "datastorage/actor.h"
#include "datastorage/index/actorindex.h"
#include "crypt/ecc/key_private.h"
#include "datastorage/profile.h"
#include "datastorage/transaction.h"

class SmartContractManager : public QObject
{
    Q_OBJECT

private:
    ActorIndex *actorIndex;
    QMap<QByteArray, QMap<QByteArray, QByteArray>> tokenBalance;
    // id wallet, id token, balance
    QVariantMap tokenId = { { "0", QVariant("Etalonium Coin") } };

private:
    void savePrivateActor(Actor<KeyPrivate> actor);
    void sendTransaction(Actor<KeyPrivate> *sender, QByteArray receiver, QByteArray quantity);
    Actor<KeyPrivate> *createContract(QByteArray tokenName);
    void initializeTokenArray();

public:
    SmartContractManager(ActorIndex *actorIndex, QObject *parent = nullptr);
    // QList<QByteArray> getAccountID();

public slots:
    void createContractProfile(QByteArray tokenCount, QByteArray tokenName, QByteArray relAddress);
    void requestTokenList();
    void process();

signals:
    // void sendTokenBalance(QMap<BigNumber,QMap<BigNumber,BigNumber>> tokenBalance);
    void sendTokenList(QVariantMap tokenList);
    void verifyActor(Actor<KeyPublic> actor);
    void sendTransactionCreateContract(Transaction trans);
    void addContractActorInActorIndex(Actor<KeyPublic> actor);
    void finished();
};

#endif // SM_CONTROLLER_H
