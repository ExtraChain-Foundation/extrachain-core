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
static constexpr int TRANSACTION_FEE = 1; // 1% from transaction amount
static constexpr int APPROVER_FEE = 5;    // 5% from TRANSACTION_FEE
enum TypeRevert
{
    Fee,
    ApproverRevert,
    CheckerRevert,
    StackRevert
};
}
#endif // COINPROCESS_H
