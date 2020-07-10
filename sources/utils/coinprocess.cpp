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

#include "headers/utils/coinprocess.h"

CoinProcess::CoinProcess(QObject* parent)
    : QObject(parent)
{
}

QList<Transaction> CoinProcess::blockDataToFeeTxs(QList<Transaction> pendingTxs, QByteArray blockHash,
                                                  BigNumber myActorId, QByteArray* companyId)
{

    static_assert(Fee::TRANSACTION_FEE > 0 && Fee::TRANSACTION_FEE < 100, "TRANSACTION_FEE fee error");
    constexpr int fee = 1000; // get fee
    QList<Transaction> feeTxs;

    Transaction temp;
    for (const auto& i : pendingTxs)
    {
        // if current transaction ==fee transaction continue
        if (i.getSender() == BigNumber(Trash::NullActor) || i.getSender() == BigNumber(*companyId)
            || i.getReceiver() == BigNumber(Trash::NullActor) || i.getReceiver() == BigNumber(*companyId)
            || i.getData() == Fee::FREEZE_TX || i.getData() == Fee::UNFREEZE_TX)
            continue;

        temp.clear();
        // else get send fee to urslf

        temp.setSender(i.getSender());
        temp.setReceiver(myActorId);
        temp.setAmount(i.getAmount() / fee);
        // ENUM | Block hash | Tx hash
        temp.setData(Serialization::serialize(
            { QByteArray::number(Fee::TypeRevert::ApproverRevert), blockHash, i.getHash() }));
        feeTxs.append(temp);
    }

    return feeTxs;
}
