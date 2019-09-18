#ifndef SM_CONTROLLER_H
#define SM_CONTROLLER_H

#include "utils/bignumber.h"
#include "datastorage/actor.h"
#include "datastorage/index/actorindex.h"
#include "crypt/ecc/key_private.h"
#include <QDebug>
#include <QObject>
#include "datastorage/profile.h"
#include "datastorage/transaction.h"

class SmartContractManager : public QObject
{
    Q_OBJECT
private:
    QByteArray m_currentToken = "0";
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
    inline void getCurrentToken()
    {
        emit sendCurrentToken(m_currentToken);
    }
    SmartContractManager(ActorIndex *actorIndex, QObject *parent = nullptr);
    QByteArray currentToken()
    {
        return m_currentToken;
    }
    // QList<QByteArray> getAccountID();
public slots:
    inline void setCurrentToken(QByteArray curToken)
    {
        qDebug() << "setCurrentToken" << curToken;
        m_currentToken = curToken;
    }
    void createContractProfile(QByteArray tokenCount, QByteArray tokenName, QByteArray relAddress);
    void requestTokenList();

public:
signals:
    // void sendTokenBalance(QMap<BigNumber,QMap<BigNumber,BigNumber>> tokenBalance);
    void sendTokenList(QVariantMap tokenList);
    void sendCurrentToken(QByteArray curToken);
    void verifyActor(Actor<KeyPublic> actor);
    void sendTransactionCreateContract(Transaction trans);
};

#endif // SM_CONTROLLER_H
