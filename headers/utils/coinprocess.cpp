#include "coinprocess.h"

CoinProcess::CoinProcess(QObject *parent)
    : QObject(parent)
{
}

QList<Transaction> CoinProcess::blockDataToFeeTxs(QList<Transaction> pendingTxs, QByteArray blockHash,
                                                  BigNumber myActorId)
{

    constexpr int fee = 100 * 100 / 5; // 5% of 1% from transaction
    QList<Transaction> feeTxs;

    Transaction temp;
    for (const auto i : pendingTxs)
    {
        // if current transaction ==fee transaction continue
        if (i.getSender() == BigNumber(Trash::NullActor))
            continue;

        temp.clear();
        // else get send fee to urslf

        temp.setSender(BigNumber(Trash::NullActor));
        temp.setReceiver(myActorId);
        temp.setAmount(i.getAmount() / fee);
        // ENUM | Block hash | Tx hash
        temp.setData(Serialization::universalSerialize(
            { QByteArray::number(Fee::TypeRevert::ApproverRevert), blockHash, i.getHash() }));
        feeTxs.append(temp);
    }

    return feeTxs;
}
