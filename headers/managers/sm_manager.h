/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

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
    void initConsoleToken(Transaction tx);
    void finished();
};

#endif // SM_CONTROLLER_H
