#ifndef COINPROCESS_H
#define COINPROCESS_H

#include <QObject>
#include "datastorage/transaction.h"
class CoinProcess : public QObject
{
    Q_OBJECT
public:
    explicit CoinProcess(QObject *parent = nullptr);

public:
    static QList<Transaction> blockDataToFeeTxs(QList<Transaction> pendingTxs, QByteArray blockHash,
                                                BigNumber myActorId);
signals:
};

namespace Fee {
enum TypeRevert
{
    ApproverRevert,
    CheckerRevert,
    StackRevert
};
}
#endif // COINPROCESS_H
