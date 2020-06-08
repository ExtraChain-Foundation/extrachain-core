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
