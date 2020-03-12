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
