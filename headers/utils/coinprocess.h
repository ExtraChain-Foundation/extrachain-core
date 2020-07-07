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

#ifndef COINPROCESS_H
#define COINPROCESS_H

#include <QObject>
#include "datastorage/transaction.h"
#include <cassert>

class CoinProcess : public QObject
{
    Q_OBJECT
public:
    explicit CoinProcess(QObject* parent = nullptr);

public:
    static QList<Transaction> blockDataToFeeTxs(QList<Transaction> pendingTxs, QByteArray blockHash,
                                                BigNumber myActorId, QByteArray* companyId);
signals:
};

namespace Fee {

static const QByteArray FREEZE_TX = "freeze";
static const QByteArray UNFREEZE_TX = "unfreeze";
static const QByteArray FEE_FREEZE_TX = "Feefreeze";
static const QByteArray FEE_UNFREEZE_TX = "FeeUNfreeze";
static const QByteArray UNFEE = "UNFee";
static const QByteArray FEE = "Fee";
static const QByteArray STAKING_REWARD = "StakingReward";

static constexpr int TRANSACTION_FEE = 1; // 1% from transaction amount
enum TypeRevert
{
    Fee,
    ApproverRevert,
    CheckerRevert,
    StackRevert
};
}
#endif // COINPROCESS_H
