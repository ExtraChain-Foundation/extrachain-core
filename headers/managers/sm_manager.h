#ifndef SM_CONTROLLER_H
#define SM_CONTROLLER_H

#include <QObject>
#include <QDebug>

#include "utils/bignumber.h"
#include "datastorage/actor.h"
#include "datastorage/index/actorindex.h"
#include "enc/key_private.h"
#include "profile/profile.h"
#include "datastorage/transaction.h"

class SmartContractManager : public QObject
{
    Q_OBJECT

private:
    ActorIndex *actorIndex;
    QMap<QByteArray, QMap<QByteArray, QByteArray>> tokenBalance;
    // id wallet, id token, balance

private:
    void savePrivateActor(Actor<KeyPrivate> actor);
    void sendInitialTransaction(Actor<KeyPrivate> *sender, QByteArray receiver, QByteArray quantity);
    Actor<KeyPrivate> *createContract(QByteArray tokenName);
    void initializeTokenArray();

public:
    SmartContractManager(ActorIndex *actorIndex, QObject *parent = nullptr);
    ~SmartContractManager() = default;
    // QList<QByteArray> getAccountID();

public slots:
    void createContractProfile(QByteArray tokenCount, QByteArray tokenName, QByteArray relAddress,
                               QByteArray color);
    void process();

signals:
    // void sendTokenBalance(QMap<BigNumber,QMap<BigNumber,BigNumber>> tokenBalance);
    void verifyActor(Actor<KeyPublic> actor);
    void sendTransactionCreateContract(const QByteArray &data, const unsigned int &type);
    //    void addContractActorInActorIndex(Actor<KeyPublic> actor);
    void saveActorInPrivateProfile(const QByteArray &id, const QString &type = "wallet",
                                   const bool &rewrite = false);
    void finished();
};

#endif // SM_CONTROLLER_H
